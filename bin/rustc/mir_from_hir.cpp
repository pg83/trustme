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

namespace {
    class ExprVisitor_Conv: public MirConverter {
        MirBuilder& m_builder;

        const ::std::vector<::HIR::TypeRef>& m_variable_types;

        /// Generators do some different codegen quirks
        bool m_is_generator;

        struct LoopDesc {
            ScopeHandle scope;
            RcString label;
            bool require_label;
            unsigned int cur;
            unsigned int next;
            ::MIR::LValue res_value;
        };

        ::std::vector<LoopDesc> m_loop_stack;

        const ScopeHandle* m_block_tmp_scope = nullptr;
        const ScopeHandle* m_borrow_raise_target = nullptr;
        const ScopeHandle* m_stmt_scope = nullptr;
        bool m_in_borrow = false;

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
            MIR::BasicBlockId bb_open;
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
        } m_generator_state;

    public:
        ExprVisitor_Conv(MirBuilder& builder, const ::std::vector<::HIR::TypeRef>& var_types, const ::HIR::ExprNode_GeneratorWrapper* is_generator)
            : m_builder(builder)
            , m_variable_types(var_types)
            , m_is_generator(is_generator != nullptr)
        {
            if (m_is_generator) {
                m_generator_state.is_future = is_generator->m_is_future;
                m_generator_state.state_idx_enm_path = is_generator->m_state_idx_enum;
                m_generator_state.bb_open = builder.pause_cur_block();
                m_generator_state.states.push_back(GeneratorState::State(builder.new_bb_unlinked()));
                builder.set_cur_block(m_generator_state.states.back().entrypoint);
            }
        }

        SaveAndEditVal<const ScopeHandle*> disable_borrow_extension() override {
            return save_and_edit(m_borrow_raise_target, nullptr);
        }

        // Get a LValue pointing at the state index
        ::MIR::LValue generator_state_lv() const {
            // (*self.ptr(?0)).state(0).value(?#1).idx(0)
            auto rv = ::MIR::LValue::new_Argument(0);
            rv = ::MIR::LValue::new_Field(mv$(rv), 0);    // .ptr (From Pin)
            rv = ::MIR::LValue::new_Deref(mv$(rv));       // .*
            rv = ::MIR::LValue::new_Field(mv$(rv), 0);    // .state
            rv = ::MIR::LValue::new_Downcast(mv$(rv), 1); // .value (From MaybeUninit)
            rv = ::MIR::LValue::new_Field(mv$(rv), 0);    // .value (From ManuallyDrop)
            rv = ::MIR::LValue::new_Field(mv$(rv), 0);    // .idx
            return rv;
        }

        const std::set<unsigned>& generator_drop_flags() const {
            return m_generator_state.saved_drop_flags;
        }

        std::set<unsigned> generator_finalise(const Span& sp, ::HIR::Enum& state_enm) {
            std::set<unsigned> used_vars;
            std::vector<MIR::BasicBlockId> arm_targets;
            arm_targets.reserve(m_generator_state.states.size() + 1);
            ::std::vector<HIR::Enum::ValueVariant> enum_variants;
            enum_variants.reserve(m_generator_state.states.size() + 1);
            for (const auto& s : m_generator_state.states) {
                arm_targets.push_back(m_builder.new_bb_unlinked());

                m_builder.set_cur_block(arm_targets.back());
                m_builder.push_stmt_assign(sp, generator_state_lv(), ::MIR::RValue::make_EnumVariant({m_generator_state.state_idx_enm_path, static_cast<unsigned>(m_generator_state.states.size()), {}}));
                m_builder.end_block(::MIR::Terminator::make_Goto(s.entrypoint));

                enum_variants.push_back(HIR::Enum::ValueVariant{RcString(), ::HIR::ExprPtr(), U128(arm_targets.size() - 1)});
                for (const auto& e : s.saved) {
                    used_vars.insert(e.first);
                }
            }
            // Final arm is the end/panic state - it's a bug to reach this
            arm_targets.push_back(m_builder.new_bb_unlinked());
            m_builder.set_cur_block(arm_targets.back());
            m_builder.end_block(::MIR::Terminator::make_Diverge({}));

            enum_variants.push_back(HIR::Enum::ValueVariant{RcString::new_interned("END"), ::HIR::ExprPtr(), U128(arm_targets.size() - 1)});
            state_enm.m_data = ::HIR::Enum::Class::make_Value({mv$(enum_variants)});

            m_builder.set_cur_block(m_generator_state.bb_open);

            // switch _n { ... }
            m_builder.end_block(::MIR::Terminator::make_Switch({generator_state_lv(), mv$(arm_targets)}));

            return used_vars;
        }

        void generator_make_drop(const Span& sp, MirBuilder& out_builder, size_t n_captures, const ::std::map<unsigned, std::vector<MIR::LValue::Wrapper>>& mappings, unsigned drop_state_field_idx, const std::map<unsigned, unsigned>& drop_flag_mapping) const {
            ::MIR::LValue self = ::MIR::LValue::new_Deref(::MIR::LValue::new_Argument(0));

            assert(m_generator_state.states.size() > 0);
            std::vector<::MIR::BasicBlockId> arms;
            arms.reserve(m_generator_state.states.size() + 1);

            // Set all drop flags from input
            if (!drop_flag_mapping.empty()) {
                auto slot = ::MIR::LValue::new_Argument(0);
                slot.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Deref());                     // Deref `&mut Self`
                slot.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Field(0));                    // Get state field
                slot.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Downcast(1));                 // .value (From MaybeUninit)
                slot.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Field(0));                    // .value (From ManuallyDrop)
                slot.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Field(drop_state_field_idx)); // drop flag bitset
                for (const auto& flag_mapping : drop_flag_mapping) {
                    auto i = out_builder.new_drop_flag(false);
                    assert(i == flag_mapping.second); // Should hold, as the map was created in-order
                    out_builder.push_stmt(
                        sp,
                        ::MIR::Statement::make_LoadDropFlag({
                            flag_mapping.first,
                            slot.clone(),
                            flag_mapping.second,
                        })
                    );
                }
            }

            auto entry_block = out_builder.pause_cur_block();
            // if state is 0, then drop captures (this is the pre-run state)
            arms.push_back(out_builder.new_bb_unlinked());
            out_builder.set_cur_block(arms.back());
            size_t arg_count = 2;
            for (size_t i = 0; i < n_captures; i++) {
                // TODO: State tracking on captures, what if a by-value capture is moved?
                if (mappings.count(arg_count + i) == 0) {
                    out_builder.push_stmt_drop(sp, ::MIR::LValue::new_Field(self.clone(), 1 + i));
                }
            }
            out_builder.end_block(::MIR::Terminator::make_Return({}));

            auto get_lv = [&sp, &self, &mappings](unsigned idx) -> ::MIR::LValue {
                ::MIR::LValue rv = self.clone();
                ASSERT_BUG(sp, mappings.count(idx), "No LValue for index " << idx);
                rv.m_wrappers.insert(rv.m_wrappers.end(), mappings.at(idx).begin(), mappings.at(idx).end());
                DEBUG("get_lv: " << rv);
                return rv;
            };

            // Else, drop yield saves (Note: final state has no saves, so acts as the "completed" state)
            for (size_t i = 0; i < m_generator_state.states.size(); i++) {
                //
                arms.push_back(out_builder.new_bb_unlinked());
                out_builder.set_cur_block(arms.back());
                for (const auto& v : m_generator_state.states[i].saved) {
                    if (v.first == 0) {
                        continue;
                    }
                    // Note: Conditional drop handled by drop flags above
                    // HACK: The caller re-maps drop flags
                    out_builder.drop_actve_local(sp, get_lv(v.first), v.second);
                }
                out_builder.end_block(::MIR::Terminator::make_Return({}));
            }
            // Generate the dispatch switch
            out_builder.set_cur_block(entry_block);
            out_builder.push_stmt_assign(sp, ::MIR::LValue::new_Return(), ::MIR::RValue::make_Tuple({}));
            auto stmt_idx_lv = mv$(self);
            stmt_idx_lv = ::MIR::LValue::new_Field(mv$(stmt_idx_lv), 0);    // .state
            stmt_idx_lv = ::MIR::LValue::new_Downcast(mv$(stmt_idx_lv), 1); // .value (From MaybeUninit)
            stmt_idx_lv = ::MIR::LValue::new_Field(mv$(stmt_idx_lv), 0);    // .value (From ManuallyDrop)
            stmt_idx_lv = ::MIR::LValue::new_Field(mv$(stmt_idx_lv), 0);    // .idx
            out_builder.end_block(::MIR::Terminator::make_Switch({mv$(stmt_idx_lv), mv$(arms)}));
        }

        // Brings variables defined in `pat` into scope
        void define_vars_from(const Span& sp, const ::HIR::Pattern& pat) override {
            for (const auto& pb : pat.m_bindings) {
                m_builder.define_variable(pb.m_slot);
            }

            TU_MATCHA(
                (pat.m_data),
                (e),
                (Any, ),
                (Box, define_vars_from(sp, *e.sub);),
                (Ref, define_vars_from(sp, *e.sub);),
                (Tuple, for (unsigned int i = 0; i < e.sub_patterns.size(); i++) { define_vars_from(sp, e.sub_patterns[i]); }),
                (SplitTuple, for (unsigned int i = 0; i < e.leading.size(); i++) define_vars_from(sp, e.leading[i]); for (unsigned int i = 0; i < e.trailing.size(); i++) define_vars_from(sp, e.trailing[i]);),
                (
                    PathValue,
                    // Nothing.
                ),
                (PathTuple, for (unsigned int i = 0; i < e.leading.size(); i++) define_vars_from(sp, e.leading[i]); for (unsigned int i = 0; i < e.trailing.size(); i++) define_vars_from(sp, e.trailing[i]);),
                (PathNamed, for (const auto& fld_pat : e.sub_patterns) { define_vars_from(sp, fld_pat.second); }),
                // Refutable
                (Value, ),
                (Range, ),
                (Slice, for (const auto& subpat : e.sub_patterns) { define_vars_from(sp, subpat); }),
                (SplitSlice, for (const auto& subpat : e.leading) { define_vars_from(sp, subpat); } if (e.extra_bind.is_valid()) { m_builder.define_variable(e.extra_bind.m_slot); } for (const auto& subpat : e.trailing) { define_vars_from(sp, subpat); }),
                (Or, assert(e.size() > 0);
                 // TODO: Save variable state, visit in order (resetting/checking after each)
                 define_vars_from(sp, e[0]);)
            )
        }

        MIR::LValue get_value_for_binding_path(const Span& sp, const ::HIR::TypeRef& outer_ty, const ::MIR::LValue& outer_lval, const PatternBinding& b) {
            HIR::TypeRef ty;
            MIR::LValue lval;
            MIR_LowerHIR_GetTypeValueForPath(sp, m_builder, outer_ty, outer_lval, b.field, ty, lval);

            if (b.is_split_slice()) {
                struct H {
                    static ::HIR::BorrowType get_borrow_type(const Span& sp, const ::HIR::PatternBinding& pb) {
                        switch (pb.m_type) {
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
                auto& types = m_builder.resolve().m_crate.m_types;
                if (const auto* tep = ty->opt_Array()) {
                    auto inner_type = tep->inner;
                    auto len = tep->size.as_Known() - sub_val_i;
                    auto ret_ty = types.array(inner_type, len);

                    if (b.binding->m_type == ::HIR::PatternBinding::Type::Move) {
                        // Create a new array value
                        std::vector<MIR::Param> array_vals;
                        for (size_t i = b.split_slice.first; i < tep->size.as_Known() - b.split_slice.second; i++) {
                            array_vals.push_back(::MIR::LValue::new_Field(lval.clone(), static_cast<unsigned>(i)));
                        }
                        lval = m_builder.lvalue_or_temp(sp, mv$(ret_ty), ::MIR::RValue::make_Array({std::move(array_vals)}));
                    } else {
                        // Create a pointer to this array, by casting a raw pointer to its first element
                        ::HIR::BorrowType bt = H::get_borrow_type(sp, *b.binding);
                        ::MIR::LValue ptr_val = m_builder.lvalue_or_temp(sp, types.pointer(bt, inner_type), ::MIR::RValue::make_Borrow({bt, true, ::MIR::LValue::new_Field(lval.clone(), static_cast<unsigned int>(b.split_slice.first))}));

                        // 3. Create a slice pointer
                        auto ptr_ty = types.pointer(bt, ret_ty);
                        lval = m_builder.lvalue_or_temp(sp, ptr_ty, ::MIR::RValue::make_Cast({mv$(ptr_val), ptr_ty}));
                        // 4. And dereference it
                        lval = ::MIR::LValue::new_Deref(std::move(lval));
                    }
                } else if (const auto* tep = ty->opt_Slice()) {
                    auto inner_type = tep->inner;

                    // 1. Obtain remaining length
                    auto usize_ty = types.primitive(::HIR::CoreType::Usize);
                    auto src_len_lval = m_builder.lvalue_or_temp(sp, usize_ty, ::MIR::RValue::make_DstMeta({m_builder.get_ptr_to_dst(sp, lval)}));
                    auto sub_val = ::MIR::Param(::MIR::Constant::make_Uint({U128(sub_val_i), ::HIR::CoreType::Usize}));
                    ::MIR::LValue len_val = m_builder.lvalue_or_temp(sp, usize_ty, ::MIR::RValue::make_BinOp({mv$(src_len_lval), ::MIR::eBinOp::SUB, mv$(sub_val)}));

                    // 2. Obtain pointer to the first element
                    // TODO: This currently emits a borrow to that element, but we need a raw pointer (to avoid being technically out-of-bounds)
                    // - Should add a MIR op for `BorrowRaw`
                    ::HIR::BorrowType bt = H::get_borrow_type(sp, *b.binding);
                    ::MIR::LValue ptr_val = m_builder.lvalue_or_temp(sp, types.pointer(bt, inner_type), ::MIR::RValue::make_Borrow({bt, true, ::MIR::LValue::new_Field(lval.clone(), static_cast<unsigned int>(b.split_slice.first))}));

                    // 3. Create a slice pointer
                    lval = m_builder.lvalue_or_temp(sp, types.borrow(bt, ty), ::MIR::RValue::make_MakeDst({mv$(ptr_val), mv$(len_val)}));
                    // 4. And dereference it
                    lval = ::MIR::LValue::new_Deref(std::move(lval));
                } else {
                    TODO(sp, "SplitSlice binding: " << b.split_slice << " - " << ty);
                }
            }

            return lval;
        }

        void destructure_from_list(const Span& sp, const ::HIR::TypeRef& outer_ty, ::MIR::LValue outer_lval, const ::std::vector<PatternBinding>& bindings, bool update_states /*=true*/) override {
            TRACE_FUNCTION_F(outer_lval << ": " << outer_ty << " [" << bindings << "]");
            // Reverse order to avoid potential use-after-move for `foo @ Bar(baz, ..)`
            for (size_t i = bindings.size(); i--;) {
                const auto& b = bindings[i];
                auto lval = get_value_for_binding_path(sp, outer_ty, outer_lval, b);

                MIR::RValue rv;
                switch (b.binding->m_type) {
                    case ::HIR::PatternBinding::Type::Move:
                        rv = mv$(lval);
                        break;
                    case ::HIR::PatternBinding::Type::Ref:
                        if (m_borrow_raise_target) {
                            DEBUG("- Raising destructure borrow of " << lval << " to scope " << *m_borrow_raise_target);
                            m_builder.raise_temporaries(sp, lval, *m_borrow_raise_target);
                        }

                        rv = ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, mv$(lval)});
                        break;
                    case ::HIR::PatternBinding::Type::MutRef:
                        if (m_borrow_raise_target) {
                            DEBUG("- Raising destructure borrow of " << lval << " to scope " << *m_borrow_raise_target);
                            m_builder.raise_temporaries(sp, lval, *m_borrow_raise_target);
                        }
                        rv = ::MIR::RValue::make_Borrow({::HIR::BorrowType::Unique, false, mv$(lval)});
                        break;
                }
                // NOTE: Don't drop the destination, as `match` does some tricky things with calling destructure multiple times (to handle or-patterns)
                m_builder.push_stmt_assign(sp, m_builder.get_variable(sp, b.binding->m_slot), mv$(rv), update_states);
            }
        }

        const HIR::TypeRef& get_binding_type(const Span& sp, unsigned index) const {
            return m_variable_types.at(index);
        }

        void emit_unwind(const Span& sp) {
            m_builder.emit_unwind_cleanup(sp);
            m_builder.end_block(::MIR::Terminator::make_Diverge({}));
        }

        // -- ExprVisitor
        void visit_node_ptr(::HIR::ExprNodeP& node_p) override {
            DEBUG(node_p.get());
            ::HIR::ExprVisitor::visit_node_ptr(node_p);
        }

        void visit(::HIR::ExprNode_Block& node) override {
            TRACE_FUNCTION_F("_Block");
            // NOTE: This doesn't create a BB, as BBs are not needed for scoping
            bool diverged = false;

            auto res_val = (node.m_value_node ? m_builder.new_temporary(node.m_res_type) : ::MIR::LValue());
            auto scope = m_builder.new_scope_var(node.span());
            auto tmp_scope = m_builder.new_scope_temp(node.span());
            auto _block_tmp_scope = save_and_edit(m_block_tmp_scope, &tmp_scope);

            for (unsigned int i = 0; i < node.m_nodes.size(); i++) {
                auto _ = this->disable_borrow_extension();
                auto& subnode = node.m_nodes[i];
                const Span& sp = subnode->span();

                auto stmt_scope = m_builder.new_scope_temp(sp);
                // NOTE: Only set the statement scope if processing a block
                auto _stmt_scope_push = save_and_edit(m_stmt_scope, dynamic_cast<::HIR::ExprNode_Block*>(subnode.get()) ? &stmt_scope : nullptr);
                this->visit_node_ptr(subnode);

                if (m_builder.block_active() || m_builder.has_result()) {
                    m_builder.get_result_in_lvalue(sp, subnode->m_res_type); // Storing in a temporary will cause a drop if this is not an lvalue
                    m_builder.terminate_scope(sp, mv$(stmt_scope));
                    diverged |= subnode->m_res_type->is_Diverge();
                } else {
                    m_builder.terminate_scope(sp, mv$(stmt_scope), false);

                    m_builder.set_cur_block(m_builder.new_bb_unlinked());
                    diverged = true;
                }
            }

            // For the last node, specially handle.
            // TODO: Any temporaries defined within this node must be elevated into the parent scope
            if (node.m_value_node) {
                auto& subnode = node.m_value_node;
                const Span& sp = subnode->span();

                auto stmt_scope = m_builder.new_scope_temp(sp);
                this->visit_node_ptr(subnode);
                if (m_builder.has_result() || m_builder.block_active()) {
                    ASSERT_BUG(sp, m_builder.block_active(), "Result yielded, but no active block");
                    ASSERT_BUG(sp, m_builder.has_result(), "Active block but no result yeilded");
                    // PROBLEM: This can drop the result before we want to use it.

                    m_builder.push_stmt_assign(sp, res_val.clone(), m_builder.get_result(sp));

                    // If this block is part of a statement, raise all temporaries from this final scope to the enclosing scope
                    if (m_stmt_scope) {
                        m_builder.raise_all(sp, mv$(stmt_scope), *m_stmt_scope);
                        //m_builder.terminate_scope(sp, mv$(stmt_scope));
                    } else {
                        m_builder.terminate_scope(sp, mv$(stmt_scope));
                    }
                    m_builder.set_result(node.span(), mv$(res_val));
                } else {
                    m_builder.terminate_scope(sp, mv$(stmt_scope), false);
                    // Block diverged in final node.
                }
                m_builder.terminate_scope(node.span(), mv$(tmp_scope), m_builder.block_active());
                m_builder.terminate_scope(node.span(), mv$(scope), m_builder.block_active());
            } else {
                if (diverged) {
                    m_builder.terminate_scope(node.span(), mv$(tmp_scope), false);
                    m_builder.terminate_scope(node.span(), mv$(scope), false);
                    m_builder.end_block(::MIR::Terminator::make_Diverge({}));
                    // Don't set a result if there's no block.
                } else {
                    m_builder.terminate_scope(node.span(), mv$(tmp_scope));
                    m_builder.terminate_scope(node.span(), mv$(scope));
                    m_builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
                }
            }
        }

        void visit(::HIR::ExprNode_ConstBlock& node) override {
            if (dynamic_cast<HIR::ExprNode_PathValue*>(node.m_inner.get())) {
                this->visit_node_ptr(node.m_inner);
            } else {
                BUG(node.span(), "Const block shouldn't have reached MIR generation");
            }
        }

        void visit(::HIR::ExprNode_Asm& node) override {
            TRACE_FUNCTION_F("_Asm");

            ::std::vector<::std::pair<::std::string, ::MIR::LValue>> inputs;
            // Inputs just need to be in lvalues
            for (auto& v : node.m_inputs) {
                this->visit_node_ptr(v.value);
                auto lv = m_builder.get_result_in_lvalue(v.value->span(), v.value->m_res_type);
                inputs.push_back(::std::make_pair(v.spec, mv$(lv)));
            }

            ::std::vector<::std::pair<::std::string, ::MIR::LValue>> outputs;
            // Outputs can also (sometimes) be rvalues (only for `*m`?)
            for (auto& v : node.m_outputs) {
                this->visit_node_ptr(v.value);
                if (v.spec[0] != '=' && v.spec[0] != '+') { // TODO: what does '+' mean?
                    ERROR(node.span(), E0000, "Assembly output specifiers must start with =");
                }
                ::MIR::LValue lv;
                if (v.spec[1] == '*') {
                    lv = m_builder.get_result_in_lvalue(v.value->span(), v.value->m_res_type);
                } else {
                    lv = m_builder.get_result_unwrap_lvalue(v.value->span());
                }
                outputs.push_back(::std::make_pair(v.spec, mv$(lv)));
            }

            m_builder.push_stmt_asm(node.span(), {node.m_template, mv$(outputs), mv$(inputs), node.m_clobbers, node.m_flags});
            m_builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
        }

        void visit(::HIR::ExprNode_Asm2& node) override {
            TRACE_FUNCTION_F("_Asm2");

            // TODO: How to represent inout in the MIR?
            // - Potentially a register specifier that links to one of the inputs
            // - OR: Just keep the parameter list as before - but now simplified to just one `Reg`
            ::MIR::Statement::Data_Asm2 ent;
            ent.options = node.m_options;
            ent.lines = node.m_lines;

            auto moved_param = [&](const ::MIR::Param& p) {
                if (const auto* e = p.opt_LValue()) {
                    m_builder.moved_lvalue(node.span(), *e);
                }
            };

            for (auto& v : node.m_params) {
                TU_MATCH_HDRA( (v), { )
                TU_ARMA(Const, e) {
                        // This constant needs to have been evaluated fully (so a `MIR::Constant` can be created)
                        this->visit_node_ptr(e);
                        auto param = m_builder.get_result_in_param(e->span(), e->m_res_type);
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
                                input = box$(m_builder.get_result_in_param(e.val->span(), e.val->m_res_type));
                                break;
                            case AsmCommon::Direction::Out:
                            case AsmCommon::Direction::LateOut:
                                if (e.val) {
                                    output = box$(m_builder.get_result_unwrap_lvalue(e.val->span()));
                                }
                                break;
                            case AsmCommon::Direction::InOut:
                            case AsmCommon::Direction::InLateOut:
                                ASSERT_BUG(node.span(), e.val, "`inout` register with no value");
                                output = box$(m_builder.get_result_unwrap_lvalue(e.val->span()));
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
                                input = box$(m_builder.get_result_in_param(e.val_in->span(), e.val_in->m_res_type));
                                assert(!e.val_out);
                                break;
                            case AsmCommon::Direction::Out:
                            case AsmCommon::Direction::LateOut:
                                ASSERT_BUG(node.span(), !e.val_in, "`[late]out` register with input value");
                                if (e.val_out) {
                                    this->visit_node_ptr(e.val_out);
                                    output = box$(m_builder.get_result_unwrap_lvalue(e.val_out->span()));
                                }
                                break;
                            case AsmCommon::Direction::InOut:
                            case AsmCommon::Direction::InLateOut:
                                ASSERT_BUG(node.span(), e.val_in, "`in[late]out` register with no input");
                                this->visit_node_ptr(e.val_in);
                                input = box$(m_builder.get_result_in_param(e.val_in->span(), e.val_in->m_res_type));
                                if (e.val_out) {
                                    this->visit_node_ptr(e.val_out);
                                    output = box$(m_builder.get_result_unwrap_lvalue(e.val_out->span()));
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
            m_builder.push_stmt(node.span(), mv$(ent));
            if (!node.m_options.noreturn) {
                m_builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
            } else {
                m_builder.end_block(::MIR::Terminator::make_Diverge({}));
            }
        }

        // Common code used by both `ExprNode_Return` and the final return of a GeneratorWrapper
        void coroutine_return(const Span& sp, const ::HIR::TypeRef& value_ty) {
            static RcString rcstring_Complete = RcString::new_interned("Complete");
            static RcString rcstring_Ready = RcString::new_interned("Ready"); // TODO: This is a lang item
            const auto& variant_name = m_generator_state.is_future ? rcstring_Ready : rcstring_Complete;
            // TODO: Handle difference between generators and futures (different return/yield types)
            ::HIR::GenericPath enm_path;
            size_t variant_index = SIZE_MAX;
            m_builder.with_val_type(sp, ::MIR::LValue::new_Return(), [&](const ::HIR::TypeRef& ty) {
                const auto& te = ty->as_Path();
                enm_path = te.path.m_data.as_Generic().clone();
                variant_index = te.binding.as_Enum()->find_variant(variant_name);
            });
            ASSERT_BUG(sp, enm_path.m_path != HIR::SimplePath(), "Failed to get path from return type?");
            ASSERT_BUG(sp, variant_index != SIZE_MAX, "Unable to find variant " << variant_name << " in " << enm_path << " for coroutine return");

            ::std::vector<::MIR::Param> values;
            values.push_back(m_builder.get_result_in_param(sp, value_ty));
            auto res = ::MIR::RValue::make_EnumVariant({std::move(enm_path), static_cast<unsigned>(variant_index), std::move(values)});
            m_builder.push_stmt_assign(sp, ::MIR::LValue::new_Return(), std::move(res));
        }

        void visit(::HIR::ExprNode_Return& node) override {
            TRACE_FUNCTION_F("_Return");
            this->visit_node_ptr(node.m_value);

            if (!m_builder.block_active()) {
                return;
            }

            if (m_is_generator) {
                coroutine_return(node.span(), node.m_value->m_res_type);
            } else {
                m_builder.push_stmt_assign(node.span(), ::MIR::LValue::new_Return(), m_builder.get_result(node.span()));
            }
            m_builder.terminate_scope_early(node.span(), m_builder.fcn_scope());
            m_builder.end_block(::MIR::Terminator::make_Return({}));
        }

        void visit(::HIR::ExprNode_Yield& node) override {
            TRACE_FUNCTION_F("_Yield");
            if (m_is_generator) {
                ASSERT_BUG(node.span(), !m_generator_state.is_future, "");

                ::HIR::GenericPath enm_path;
                m_builder.with_val_type(node.span(), ::MIR::LValue::new_Return(), [&](const ::HIR::TypeRef& ty) {
                    const auto& te = ty->as_Path();
                    enm_path = te.path.m_data.as_Generic().clone();
                    ASSERT_BUG(node.span(), te.binding.as_Enum()->find_variant("Yielded") == 0, "");
                });

                this->visit_node_ptr(node.m_value);
                // Emit return, wrapped in GeneratorState::Yielded
                ::std::vector<::MIR::Param> values;
                values.push_back(m_builder.get_result_in_param(node.span(), node.m_value->m_res_type));
                auto res = ::MIR::RValue::make_EnumVariant(
                    {mv$(enm_path),
                     0, // Yielded is the first variant
                     mv$(values)}
                );
                m_builder.push_stmt_assign(node.span(), ::MIR::LValue::new_Return(), mv$(res));
                m_builder.push_stmt_assign(node.span(), generator_state_lv(), ::MIR::RValue::make_EnumVariant({m_generator_state.state_idx_enm_path.clone(), static_cast<unsigned>(m_generator_state.states.size()), {}}));
                // NOTE: No scope terminate
                m_builder.end_block(::MIR::Terminator::make_Return({}));

                m_generator_state.states.back().saved = m_builder.get_active_locals(node.span(), m_generator_state.saved_drop_flags);
                m_generator_state.states.push_back(m_builder.new_bb_unlinked());
                m_builder.set_cur_block(m_generator_state.states.back().entrypoint);

                m_builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
            } else {
                BUG(node.span(), "Unexpected ExprNode_Yield (should have been re-written)");
            }
        }

        void visit(::HIR::ExprNode_AWait& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_AWait");
            ASSERT_BUG(node.span(), m_is_generator && m_generator_state.is_future, "`.await` not in an async block/function");
            const auto& ty_inner = node.m_value->m_res_type;

            this->visit_node_ptr(node.m_value);
            auto lv_res = m_builder.get_result_in_lvalue(sp, ty_inner);

            auto state_value = static_cast<unsigned>(m_generator_state.states.size());
            m_generator_state.states.back().saved = m_builder.get_active_locals(node.span(), m_generator_state.saved_drop_flags);
            m_generator_state.states.push_back(m_builder.new_bb_unlinked());
            m_builder.end_block(m_generator_state.states.back().entrypoint);
            m_builder.set_cur_block(m_generator_state.states.back().entrypoint);

            // Create `Pin<&mut >` as the reciever, using `Pin::new_unchecked`
            const auto& lang_Pin = m_builder.resolve().m_crate.get_lang_item_path(sp, "pin");
            auto& types = m_builder.resolve().m_crate.m_types;
            auto type_mut = types.borrow(::HIR::BorrowType::Unique, ty_inner);
            auto pin_path = ::HIR::GenericPath(lang_Pin, ::HIR::PathParams(type_mut));
            auto type_pin = types.path(std::move(pin_path), &m_builder.resolve().m_crate.get_struct_by_path(sp, lang_Pin));

            auto lv_mut = m_builder.lvalue_or_temp(sp, type_mut, ::MIR::RValue::make_Borrow({::HIR::BorrowType::Unique, false, std::move(lv_res)}));
            auto lv_pin = m_builder.new_temporary(type_pin);
            {
                auto bb_ret = m_builder.new_bb_unlinked();
                auto bb_panic = m_builder.new_bb_unlinked();
                m_builder.end_block(::MIR::Terminator::make_Call({bb_ret, bb_panic, lv_pin.clone(), ::HIR::Path(type_pin, "new_unchecked"), make_vec1(::MIR::Param(lv_mut.clone()))}));
                m_builder.moved_lvalue(node.span(), std::move(lv_mut));
                m_builder.set_cur_block(bb_panic);
                emit_unwind(sp);
                m_builder.set_cur_block(bb_ret);
            }
            // Call `Future::poll`
            const auto& lang_Poll = m_builder.resolve().m_crate.get_lang_item_path(sp, "Poll");
            auto type_poll = types.path(::HIR::GenericPath(lang_Poll, ::HIR::PathParams(node.m_res_type)), &m_builder.resolve().m_crate.get_enum_by_path(sp, lang_Poll));
            auto lv_poll = m_builder.new_temporary(type_poll);
            {
                auto bb_ret = m_builder.new_bb_unlinked();
                auto bb_panic = m_builder.new_bb_unlinked();
                m_builder.end_block(
                    ::MIR::Terminator::make_Call(
                        {bb_ret,
                         bb_panic,
                         lv_poll.clone(),
                         ::HIR::Path(ty_inner, m_builder.resolve().m_lang_Future, "poll"),
                         make_vec2(
                             ::MIR::Param(lv_pin.clone()),
                             ::MIR::Param::make_Borrow({
                                 ::HIR::BorrowType::Unique,
                                 ::MIR::LValue::new_Deref(::MIR::LValue::new_Argument(1)) // Context is the second argument (first is `self`)
                             })
                         )}
                    )
                );
                m_builder.moved_lvalue(node.span(), std::move(lv_pin));
                m_builder.set_cur_block(bb_panic);
                emit_unwind(sp);
                m_builder.set_cur_block(bb_ret);
            }
            // Check return
            const auto variant_Ready = 0;
            {
                auto bb_pending = m_builder.new_bb_unlinked();
                auto bb_ready = m_builder.new_bb_unlinked();
                ASSERT_BUG(node.span(), type_poll->as_Path().binding.as_Enum()->find_variant("Ready") == variant_Ready, "");
                ASSERT_BUG(node.span(), type_poll->as_Path().binding.as_Enum()->find_variant("Pending") == 1, "");
                m_builder.end_block(::MIR::Terminator::make_Switch({lv_poll.clone(), make_vec2(bb_ready, bb_pending)}));
                m_builder.set_cur_block(bb_pending);

                // `retval = ::core::task::Poll::Pending; RETURN`
                HIR::GenericPath path_local_Poll;
                m_builder.with_val_type(sp, ::MIR::LValue::new_Return(), [&](const ::HIR::TypeRef& ty) {
                    path_local_Poll = ty->as_Path().path.m_data.as_Generic().clone();
                });
                m_builder.push_stmt_assign(node.span(), ::MIR::LValue::new_Return(), ::MIR::RValue::make_EnumVariant({std::move(path_local_Poll), 1, {}}));
                m_builder.push_stmt_assign(node.span(), generator_state_lv(), ::MIR::RValue::make_EnumVariant({m_generator_state.state_idx_enm_path.clone(), state_value, {}}));
                m_builder.end_block(::MIR::Terminator::make_Return({}));

                m_builder.set_cur_block(bb_ready);
            }
            // lv_poll.#0.0 to get the field of the first variant
            m_builder.set_result(node.span(), ::MIR::LValue::new_Field(::MIR::LValue::new_Downcast(std::move(lv_poll), variant_Ready), 0));
        }

        void visit(::HIR::ExprNode_Let& node) override {
            TRACE_FUNCTION_F("_Let " << node.m_pattern);
            this->define_vars_from(node.span(), node.m_pattern);
            if (node.m_value) {
                auto _ = save_and_edit(m_borrow_raise_target, m_block_tmp_scope);
                this->visit_node_ptr(node.m_value);

                if (!m_builder.block_active()) {
                    return;
                }
                auto res = m_builder.get_result(node.span());

                // Shortcut for `let foo = bar;` (avoids the extra temporary that would need to be optimised out)
                if (node.m_pattern.m_data.is_Any() && !node.m_pattern.m_bindings.empty() && std::all_of(node.m_pattern.m_bindings.begin(), node.m_pattern.m_bindings.end(), [](const HIR::PatternBinding& pb) {
                    return pb.m_type == ::HIR::PatternBinding::Type::Move;
                })) {
                    for (const auto& pb : node.m_pattern.m_bindings) {
                        m_builder.push_stmt_assign(node.span(), m_builder.get_variable(node.span(), pb.m_slot), mv$(res));
                    }
                } else {
                    MIR_LowerHIR_Let(m_builder, *this, node.span(), node.m_pattern, m_builder.lvalue_or_temp(node.m_value->span(), node.m_type, mv$(res)), nullptr);
                }
            }
            m_builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
        }

        void visit(::HIR::ExprNode_Loop& node) override {
            TRACE_FUNCTION_FR("_Loop", "_Loop");
            auto loop_block = m_builder.new_bb_linked();
            auto loop_body_scope = m_builder.new_scope_loop(node.span());
            auto loop_next = m_builder.new_bb_unlinked();

            auto loop_result_lvaue = m_builder.new_temporary(node.m_res_type);

            auto loop_tmp_scope = m_builder.new_scope_temp(node.span());
            auto _ = save_and_edit(m_stmt_scope, &loop_tmp_scope);

            m_loop_stack.push_back(LoopDesc{mv$(loop_body_scope), node.m_label, node.m_require_label, loop_block, loop_next, loop_result_lvaue.clone()});
            this->visit_node_ptr(node.m_code);
            auto loop_scope = mv$(m_loop_stack.back().scope);
            m_loop_stack.pop_back();

            // If there's a stray result, drop it
            if (m_builder.has_result()) {
                assert(m_builder.block_active());
                // TODO: Properly drop this? Or just discard it? It should be ()
                m_builder.get_result(node.span());
            }
            // Terminate block with a jump back to the start
            // - Also inserts the jump if this didn't uncondtionally diverge
            if (m_builder.block_active()) {
                DEBUG("- Reached end, loop back");
                // Insert drop of all scopes within the current scope
                m_builder.terminate_scope(node.span(), mv$(loop_tmp_scope));
                m_builder.terminate_scope(node.span(), mv$(loop_scope));
                m_builder.end_block(::MIR::Terminator::make_Goto(loop_block));
            } else {
                // Terminate scope without emitting cleanup (cleanup was handled by `break`)
                m_builder.terminate_scope(node.span(), mv$(loop_tmp_scope), false);
                m_builder.terminate_scope(node.span(), mv$(loop_scope), false);
            }

            if (!node.m_diverges) {
                DEBUG("- Doesn't diverge");
                m_builder.set_cur_block(loop_next);
                m_builder.set_result(node.span(), mv$(loop_result_lvaue));
            } else {
                DEBUG("- Diverges");
                assert(!m_builder.has_result());

                m_builder.set_cur_block(loop_next);
                m_builder.end_split_arm_early(node.span());
                assert(!m_builder.has_result());
                m_builder.end_block(::MIR::Terminator::make_Diverge({}));
            }

            // TODO: Store the variable state on a break for restoration at the end of the loop.
        }

        /// Locate a loop given a name
        const LoopDesc& find_loop(const Span& sp, const RcString& target_label) const {
            if (target_label != "") {
                auto it = ::std::find_if(m_loop_stack.rbegin(), m_loop_stack.rend(), [&](const auto& x) {
                    return x.label == target_label;
                });
                if (it == m_loop_stack.rend()) {
                    BUG(sp, "Named loop '" << target_label << " doesn't exist");
                }
                return *it;
            } else {
                auto it = ::std::find_if(m_loop_stack.rbegin(), m_loop_stack.rend(), [](const auto& x) {
                    return !x.require_label;
                });
                if (it == m_loop_stack.rend()) {
                    BUG(sp, "Break outside of a breakable block");
                }
                if (it->label != "" && it->label.c_str()[0] == '#') {
                    TODO(sp, "Break within try block, want to break parent loop instead");
                }
                return *it;
            }
        }

        void visit(::HIR::ExprNode_LoopControl& node) override {
            TRACE_FUNCTION_F("_LoopControl \"" << node.m_label << "\"");
            if (m_loop_stack.size() == 0) {
                BUG(node.span(), "Loop control outside of a loop");
            }

            // Visit value before looking up the loop (loop stack may be manipulated during the inner visit)
            if (node.m_value) {
                ASSERT_BUG(node.span(), !node.m_continue, "Continue with a value isn't valid");
                DEBUG("break value;");
                this->visit_node_ptr(node.m_value);
                //if( m_builder.resolve().type_is_impossible(node.span(), node.m_value->m_res_type) ) {
                if (node.m_value->m_res_type->is_Diverge()) {
                    //ASSERT_BUG(node.span(), !m_builder.has_result(), "Result present when value type is uninhabited - " << node.m_value->m_res_type);
                    //ASSERT_BUG(node.span(), !m_builder.block_active(), "Result present when value type is uninhabited - " << node.m_value->m_res_type);
                }
            }
            if (!m_builder.block_active()) {
                // No block is currently active, not worth running the rest
                return;
            }

            // TODO: Use node.m_target_node
            const LoopDesc& target_block = this->find_loop(node.span(), node.m_label);

            if (node.m_continue) {
                m_builder.terminate_scope_early(node.span(), target_block.scope, /*loop_exit=*/false);
                m_builder.end_block(::MIR::Terminator::make_Goto(target_block.cur));
            } else {
                if (node.m_value) {
                    m_builder.push_stmt_assign(node.span(), target_block.res_value.clone(), m_builder.get_result(node.span()));
                } else {
                    // Set result to ()
                    m_builder.push_stmt_assign(node.span(), target_block.res_value.clone(), ::MIR::RValue::make_Tuple({{}}));
                }
                m_builder.terminate_scope_early(node.span(), target_block.scope, /*loop_exit=*/true);
                m_builder.end_block(::MIR::Terminator::make_Goto(target_block.next));
            }
        }

        void visit(::HIR::ExprNode_Match& node) override {
            TRACE_FUNCTION_FR("_Match", "_Match");
            {
                auto _ = save_and_edit(m_borrow_raise_target, nullptr);
                //auto stmt_scope = m_builder.new_scope_temp(node.span());
                this->visit_node_ptr(node.m_value);
            }
            if (!m_builder.block_active()) {
                return;
            }
            auto match_val = m_builder.get_result_in_lvalue(node.m_value->span(), node.m_value->m_res_type);

            if (node.m_arms.size() == 0) {
                // Nothing
                //const auto& ty = node.m_value->m_res_type;
                // TODO: Ensure that the type is a zero-variant enum or !
                m_builder.end_split_arm_early(node.span());
                m_builder.end_block(::MIR::Terminator::make_Diverge({}));
                // Push an "diverge" result
                //m_builder.set_cur_block( m_builder.new_bb_unlinked() );
                //m_builder.set_result(node.span(), ::MIR::LValue::make_Invalid({}) );
            } else {
                MIR_LowerHIR_Match(m_builder, *this, node, mv$(match_val));
            }

            if (m_builder.block_active()) {
                const auto& sp = node.span();

                auto res = m_builder.get_result(sp);
                //m_builder.raise_variables(sp, res, stmt_scope, /*to_above=*/true);
                m_builder.set_result(sp, mv$(res));

                //m_builder.terminate_scope( node.span(), mv$(stmt_scope) );
            } else {
                //m_builder.terminate_scope( node.span(), mv$(stmt_scope), false );
            }
        } // ExprNode_Match

        void emit_if(/*const*/ ::HIR::ExprNodeP& cond, ::MIR::BasicBlockId true_branch, ::MIR::BasicBlockId false_branch) {
            TRACE_FUNCTION_F("true=bb" << true_branch << ", false=bb" << false_branch);
            auto* cond_p = &cond;

            // - Convert ! into a reverse of the branches
            {
                bool reverse = false;
                while (auto* cond_uni = dynamic_cast<::HIR::ExprNode_UniOp*>(cond_p->get())) {
                    ASSERT_BUG(cond_uni->span(), cond_uni->m_op == ::HIR::ExprNode_UniOp::Op::Invert, "Unexpected UniOp on boolean in `if` condition");
                    cond_p = &cond_uni->m_value;
                    reverse = !reverse;
                }

                if (reverse) {
                    ::std::swap(true_branch, false_branch);
                }
            }

            // Short-circuit && and ||
            if (auto* cond_bin = dynamic_cast<::HIR::ExprNode_BinOp*>(cond_p->get())) {
                switch (cond_bin->m_op) {
                    case ::HIR::ExprNode_BinOp::Op::BoolAnd:
                    case ::HIR::ExprNode_BinOp::Op::BoolOr: {
                        // TODO: Generate a SplitScope
                        if (cond_bin->m_op == ::HIR::ExprNode_BinOp::Op::BoolAnd) {
                            DEBUG("- Short-circuit BoolAnd");

                            // IF left false: go to false immediately
                            auto inner_true_branch = m_builder.new_bb_unlinked();
                            emit_if(cond_bin->m_left, inner_true_branch, false_branch);
                            // ELSE use right
                            m_builder.set_cur_block(inner_true_branch);
                        } else {
                            DEBUG("- Short-circuit BoolOr");

                            // IF left true: got to true
                            auto inner_false_branch = m_builder.new_bb_unlinked();
                            emit_if(cond_bin->m_left, true_branch, inner_false_branch);
                            // ELSE use right
                            m_builder.set_cur_block(inner_false_branch);
                        }

                        auto split_scope = m_builder.new_scope_split(cond_bin->span());
                        m_builder.end_split_arm(cond_bin->span(), split_scope, /*reachable=*/true);
                        auto final_true_branch = m_builder.new_bb_unlinked();
                        auto final_false_branch = m_builder.new_bb_unlinked();
                        emit_if(cond_bin->m_right, final_true_branch, final_false_branch);

                        m_builder.set_cur_block(final_false_branch);
                        m_builder.end_split_arm(cond_bin->span(), split_scope, /*reachable=*/true, true);
                        m_builder.end_block(MIR::Terminator::make_Goto(false_branch));

                        m_builder.set_cur_block(final_true_branch);
                        m_builder.end_split_arm(cond_bin->span(), split_scope, /*reachable=*/true);
                        m_builder.terminate_scope(cond_bin->span(), std::move(split_scope));
                        m_builder.end_block(MIR::Terminator::make_Goto(true_branch));
                    }
                        return;
                    default:
                        break;
                }
            }

            if (auto* cond_lit = dynamic_cast<::HIR::ExprNode_Literal*>(cond_p->get())) {
                DEBUG("- constant condition");
                if (cond_lit->m_data.as_Boolean()) {
                    m_builder.end_block(::MIR::Terminator::make_Goto(true_branch));
                } else {
                    m_builder.end_block(::MIR::Terminator::make_Goto(false_branch));
                }
                return;
            }

            // If short-circuiting didn't apply, emit condition
            ::MIR::LValue decision_val;
            {
                auto scope = m_builder.new_scope_temp(cond->span());
                this->visit_node_ptr(*cond_p);
                ASSERT_BUG(cond->span(), cond->m_res_type == ::HIR::CoreType::Bool, "If condition wasn't a bool");
                decision_val = m_builder.get_result_in_if_cond(cond->span());
                m_builder.terminate_scope(cond->span(), mv$(scope));
            }

            m_builder.end_block(::MIR::Terminator::make_If({mv$(decision_val), true_branch, false_branch}));
        }

        void generate_checked_binop(const Span& sp, ::MIR::LValue res_slot, ::MIR::eBinOp op, ::MIR::Param val_l, const ::HIR::TypeRef& ty_l, ::MIR::Param val_r, const ::HIR::TypeRef& ty_r) {
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
                m_builder.push_stmt_assign(sp, mv$(res_slot), ::MIR::RValue::make_BinOp({ mv$(val_l), op, mv$(val_r) }));
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
                m_builder.push_stmt_assign(sp, mv$(res_slot), ::MIR::RValue::make_BinOp({ mv$(val_l), op, mv$(val_r) }));
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
                m_builder.push_stmt_assign(sp, mv$(res_slot), ::MIR::RValue::make_BinOp({ mv$(val_l), op, mv$(val_r) }));
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
                m_builder.push_stmt_assign(sp, mv$(res_slot), ::MIR::RValue::make_BinOp({ mv$(val_l), op, mv$(val_r) }));
                break;
            }
        }

        void visit(::HIR::ExprNode_Assign& node) override {
            TRACE_FUNCTION_F("_Assign");
            const auto& sp = node.span();
            auto _ = disable_borrow_extension(); // A bit of a hack

            this->visit_node_ptr(node.m_value);
            ::MIR::RValue val = m_builder.get_result(sp);

            this->visit_node_ptr(node.m_slot);
            auto dst = m_builder.get_result_unwrap_lvalue(sp);

            const auto& ty_slot = node.m_slot->m_res_type;
            const auto& ty_val = node.m_value->m_res_type;

            if (node.m_op != ::HIR::ExprNode_Assign::Op::None) {
                auto dst_clone = dst.clone();
                ::MIR::Param val_p;
                if (auto* e = val.opt_Use()) {
                    val_p = mv$(*e);
                } else if (auto* e = val.opt_Constant()) {
                    val_p = mv$(*e);
                } else {
                    val_p = m_builder.lvalue_or_temp(node.span(), ty_val, mv$(val));
                }

                ASSERT_BUG(sp, ty_slot->is_Primitive(), "Assignment operator overloads are only valid on primitives - ty_slot=" << ty_slot);
                ASSERT_BUG(sp, ty_val->is_Primitive(), "Assignment operator overloads are only valid on primitives - ty_val=" << ty_val);

#define _(v) ::HIR::ExprNode_Assign::Op::v
                ::MIR::eBinOp op;
                switch (node.m_op) {
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
                        this->generate_checked_binop(sp, mv$(dst), op, mv$(dst_clone), ty_slot, mv$(val_p), ty_val);
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
                        this->generate_checked_binop(sp, mv$(dst), op, mv$(dst_clone), ty_slot, mv$(val_p), ty_val);
                        break;
                    case _(Shl):
                        op = ::MIR::eBinOp::BIT_SHL;
                        if (0) {
                            case _(Shr):
                                op = ::MIR::eBinOp::BIT_SHR;
                        }
                        this->generate_checked_binop(sp, mv$(dst), op, mv$(dst_clone), ty_slot, mv$(val_p), ty_val);
                        break;
                }
#undef _
            } else {
                ASSERT_BUG(sp, ty_slot == ty_val || ty_slot->equals_ignoring_regions(ty_val), "Types must match for assignment - " << ty_slot << " != " << ty_val);
                m_builder.push_stmt_assign(node.span(), mv$(dst), mv$(val));
            }
            m_builder.set_result(node.span(), ::MIR::RValue::make_Tuple({}));
        }

        void visit(::HIR::ExprNode_BinOp& node) override {
            const auto& sp = node.span();
            TRACE_FUNCTION_F("_BinOp");

            const auto& ty_l = node.m_left->m_res_type;
            const auto& ty_r = node.m_right->m_res_type;
            auto res = m_builder.new_temporary(node.m_res_type);

            // Short-circuiting boolean operations
            if (node.m_op == ::HIR::ExprNode_BinOp::Op::BoolAnd || node.m_op == ::HIR::ExprNode_BinOp::Op::BoolOr) {
                DEBUG("- ShortCircuit Left");
                this->visit_node_ptr(node.m_left);
                if (!m_builder.block_active()) {
                    return;
                }
                auto left = m_builder.get_result_in_lvalue(node.m_left->span(), ty_l);

                auto bb_next = m_builder.new_bb_unlinked();
                auto bb_true = m_builder.new_bb_unlinked();
                auto bb_false = m_builder.new_bb_unlinked();
                m_builder.end_block(::MIR::Terminator::make_If({mv$(left), bb_true, bb_false}));

                // Generate a SplitScope to handle the conditional nature of the next code
                auto split_scope = m_builder.new_scope_split(node.span());

                if (node.m_op == ::HIR::ExprNode_BinOp::Op::BoolOr) {
                    DEBUG("- ShortCircuit ||");
                    // If left is true, assign result true and return
                    m_builder.set_cur_block(bb_true);
                    m_builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue(::MIR::Constant::make_Bool({true})));
                    m_builder.end_split_arm(node.m_left->span(), split_scope, /*reachable=*/true);
                    m_builder.end_block(::MIR::Terminator::make_Goto(bb_next));

                    // If left is false, assign result to right
                    m_builder.set_cur_block(bb_false);
                } else {
                    DEBUG("- ShortCircuit &&");
                    // If left is false, assign result false and return
                    m_builder.set_cur_block(bb_false);
                    m_builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue(::MIR::Constant::make_Bool({false})));
                    m_builder.end_split_arm(node.m_left->span(), split_scope, /*reachable=*/true);
                    m_builder.end_block(::MIR::Terminator::make_Goto(bb_next));

                    // If left is true, assign result to right
                    m_builder.set_cur_block(bb_true);
                }

                DEBUG("- ShortCircuit Right");
                auto tmp_scope = m_builder.new_scope_temp(node.m_right->span());
                this->visit_node_ptr(node.m_right);
                if (!m_builder.block_active()) {
                    m_builder.terminate_scope(node.m_right->span(), mv$(tmp_scope), false);
                    m_builder.end_split_arm(node.m_right->span(), split_scope, /*reachable=*/false);
                    m_builder.set_cur_block(bb_next);
                    m_builder.terminate_scope(node.span(), mv$(split_scope));
                    m_builder.set_result(node.span(), mv$(res));
                    return;
                }
                m_builder.push_stmt_assign(node.span(), res.clone(), m_builder.get_result(node.m_right->span()));
                m_builder.terminate_scope(node.m_right->span(), mv$(tmp_scope));

                m_builder.end_split_arm(node.m_right->span(), split_scope, /*reachable=*/true);
                m_builder.end_block(::MIR::Terminator::make_Goto(bb_next));

                m_builder.set_cur_block(bb_next);
                m_builder.terminate_scope(node.span(), mv$(split_scope));
                m_builder.set_result(node.span(), mv$(res));
                return;
            } else {
            }

            this->visit_node_ptr(node.m_left);
            if (!m_builder.block_active()) {
                return;
            }
            auto left = m_builder.get_result_in_param(node.m_left->span(), ty_l);
            this->visit_node_ptr(node.m_right);
            if (!m_builder.block_active()) {
                return;
            }
            auto right = m_builder.get_result_in_param(node.m_right->span(), ty_r);

            ::MIR::eBinOp op;
            switch (node.m_op) {
                case ::HIR::ExprNode_BinOp::Op::CmpEqu:
                    op = ::MIR::eBinOp::EQ;
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::CmpNEqu:
                            op = ::MIR::eBinOp::NE;
                    }
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::CmpLt:
                            op = ::MIR::eBinOp::LT;
                    }
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::CmpLtE:
                            op = ::MIR::eBinOp::LE;
                    }
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::CmpGt:
                            op = ::MIR::eBinOp::GT;
                    }
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::CmpGtE:
                            op = ::MIR::eBinOp::GE;
                    }
                    this->generate_checked_binop(sp, res.clone(), op, mv$(left), ty_l, mv$(right), ty_r);
                    break;

                case ::HIR::ExprNode_BinOp::Op::Xor:
                    op = ::MIR::eBinOp::BIT_XOR;
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::Or:
                            op = ::MIR::eBinOp::BIT_OR;
                    }
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::And:
                            op = ::MIR::eBinOp::BIT_AND;
                    }
                    this->generate_checked_binop(sp, res.clone(), op, mv$(left), ty_l, mv$(right), ty_r);
                    break;

                case ::HIR::ExprNode_BinOp::Op::Shr:
                    op = ::MIR::eBinOp::BIT_SHR;
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::Shl:
                            op = ::MIR::eBinOp::BIT_SHL;
                    }
                    this->generate_checked_binop(sp, res.clone(), op, mv$(left), ty_l, mv$(right), ty_r);
                    break;

                case ::HIR::ExprNode_BinOp::Op::Add:
                    op = ::MIR::eBinOp::ADD;
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::Sub:
                            op = ::MIR::eBinOp::SUB;
                    }
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::Mul:
                            op = ::MIR::eBinOp::MUL;
                    }
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::Div:
                            op = ::MIR::eBinOp::DIV;
                    }
                    if (0) {
                        case ::HIR::ExprNode_BinOp::Op::Mod:
                            op = ::MIR::eBinOp::MOD;
                    }
                    this->generate_checked_binop(sp, res.clone(), op, mv$(left), ty_l, mv$(right), ty_r);
                    break;

                // Short-circuiting boolean operations
                case ::HIR::ExprNode_BinOp::Op::BoolAnd:
                case ::HIR::ExprNode_BinOp::Op::BoolOr:
                    BUG(node.span(), "");
                    break;
            }
            m_builder.set_result(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNode_UniOp& node) override {
            TRACE_FUNCTION_F("_UniOp");

            const auto& ty_val = node.m_value->m_res_type;
            this->visit_node_ptr(node.m_value);
            auto val = m_builder.get_result_in_lvalue(node.m_value->span(), ty_val);

            ::MIR::RValue res;
            switch (node.m_op) {
                case ::HIR::ExprNode_UniOp::Op::Invert:
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
                case ::HIR::ExprNode_UniOp::Op::Negate:
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
            m_builder.set_result(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNode_Borrow& node) override {
            TRACE_FUNCTION_F("_Borrow");

            auto _ = save_and_edit(m_in_borrow, true);

            const auto& ty_val = node.m_value->m_res_type;
            this->visit_node_ptr(node.m_value);
            auto val = m_builder.get_result_in_lvalue(node.m_value->span(), ty_val);

            if (m_borrow_raise_target) {
                DEBUG("- Raising borrow to scope " << *m_borrow_raise_target);
                m_builder.raise_temporaries(node.span(), val, *m_borrow_raise_target);
            }

            m_builder.set_result(node.span(), ::MIR::RValue::make_Borrow({node.m_type, false, mv$(val)}));
        }

        void visit(::HIR::ExprNode_RawBorrow& node) override {
            TRACE_FUNCTION_F("_RawBorrow");

            auto _ = save_and_edit(m_in_borrow, true);

            const auto& ty_val = node.m_value->m_res_type;
            this->visit_node_ptr(node.m_value);
            auto val = m_builder.get_result_in_lvalue(node.m_value->span(), ty_val);

            if (m_borrow_raise_target) {
                DEBUG("- Raising borrow to scope " << *m_borrow_raise_target);
                m_builder.raise_temporaries(node.span(), val, *m_borrow_raise_target);
            }

            m_builder.set_result(node.span(), ::MIR::RValue::make_Borrow({node.m_type, true, mv$(val)}));
        }

        void visit(::HIR::ExprNode_Cast& node) override {
            TRACE_FUNCTION_F("_Cast " << node.m_res_type);
            this->visit_node_ptr(node.m_value);

            const auto& ty_out = node.m_res_type;
            const auto& ty_in = node.m_value->m_res_type;

            // TODO: The correct behavior is to do the cast (into a rvalue) no matter what.
            // See test run-pass/issue-36936
            if (ty_out == ty_in) {
                return;
            }

            auto val = m_builder.get_result_in_lvalue(node.m_value->span(), node.m_value->m_res_type);

            TU_MATCH_HDRA( (*ty_out), {)
            default:
                BUG(node.span(), "Invalid cast to " << ty_out << " from " << ty_in);
                TU_ARMA(Function, de) {
                    // Just trust the previous stages.
                    if (ty_in->is_Function()) {
                        ASSERT_BUG(node.span(), de.m_arg_types == ty_in->as_Function().m_arg_types, ty_in);
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
                        if (de.inner != se->inner && !de.inner->equals_ignoring_regions(se->inner)) {
                            BUG(node.span(), "Cannot cast to " << ty_out << " from " << ty_in);
                        }
                        // Valid
                    } else if (ty_in->is_Function() || ty_in->is_NamedFunction()) {
                        if (!m_builder.resolve().type_is_sized(node.span(), de.inner)) {
                            BUG(node.span(), "Cannot cast to " << ty_out << " from " << ty_in);
                        }
                        // Valid
                    } else if (const auto* se = ty_in->opt_Pointer()) {
                        // Valid
                        if (se->inner == de.inner) {
                        }
                        // - If making a fat pointer from thin, convert to _Unsize
                        else if (m_builder.resolve().can_unsize(node.span(), de.inner, se->inner)) {
                            m_builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(val), ::MIR::Constant::make_ItemAddr({})}));
                            auto tmp_ty = m_builder.resolve().m_crate.m_types.pointer(se->type, de.inner);
                            val = m_builder.get_result_in_lvalue(node.m_value->span(), tmp_ty);
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
            auto res = m_builder.new_temporary(node.m_res_type);
            m_builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue::make_Cast({ mv$(val), node.m_res_type }));
            m_builder.set_result( node.span(), mv$(res) );
        }

        void visit(::HIR::ExprNode_Unsize& node) override {
            TRACE_FUNCTION_F("_Unsize");
            this->visit_node_ptr(node.m_value);

            const auto& ty_out = node.m_res_type;
            const auto& ty_in = node.m_value->m_res_type;

            if (ty_out == ty_in) {
                return;
            }

            auto ptr_lval = m_builder.get_result_in_lvalue(node.m_value->span(), node.m_value->m_res_type);

            if (ty_out->is_Borrow() && ty_in->is_Borrow()) {
                const auto& oe = ty_out->as_Borrow();
                const auto& ie = ty_in->as_Borrow();
                const auto& ty_out = oe.inner;
                const auto& ty_in = ie.inner;
                TU_MATCH_HDRA( (*ty_out), {)
                default: {
                        const auto& lang_Unsize = m_builder.crate().get_lang_item_path(node.span(), "unsize");
                        if (m_builder.resolve().find_impl(node.span(), lang_Unsize, ::HIR::PathParams(ty_out), ty_in, [](auto, bool) {
                            return true;
                        })) {
                            // - HACK: Emit a cast operation on the pointers. Leave it up to monomorph to 'fix' it
                            m_builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptr_lval), ::MIR::Constant::make_ItemAddr({})}));
                        } else {
                            // Probably an error?
                            m_builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptr_lval), ::MIR::Constant::make_ItemAddr({})}));
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
                        m_builder.set_result( node.span(), ::MIR::RValue::make_MakeDst({ mv$(ptr_lval), mv$(size_val) }) );
                        } else if (ty_in->is_Generic() || (ty_in->is_Path() && ty_in->as_Path().binding.is_Opaque())) {
                            // The source is thin here: its concrete array length becomes
                            // available only after monomorphisation. Preserve the unsize
                            // sentinel for MIR cleanup instead of reading nonexistent metadata.
                            m_builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptr_lval), ::MIR::Constant::make_ItemAddr({})}));
                        } else {
                            ASSERT_BUG(node.span(), ty_in->is_Array(), "Unsize to slice from non-array - " << ty_in);
                        }
                    }
                    TU_ARMA(TraitObject, e) {
                        // NOTE: This pattern (an empty ItemAddr) is detected by cleanup, which populates the vtable properly
                        m_builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptr_lval), ::MIR::Constant::make_ItemAddr({})}));
                    }
                }
            } else {
                // NOTES: (from IRC: eddyb)
                // < eddyb> they're required that T and U are the same struct definition (with different type parameters) and exactly one field differs in type between T and U (ignoring PhantomData)
                // < eddyb> Mutabah: I forgot to mention that the field that differs in type must also impl CoerceUnsized

                // TODO: Just emit a cast and leave magic handling to codegen
                // - This code _could_ do inspection of the types and insert a destructure+unsize+restructure, but that does't handle direct `T: CoerceUnsize<U>`
                m_builder.set_result(node.span(), ::MIR::RValue::make_MakeDst({mv$(ptr_lval), ::MIR::Constant::make_ItemAddr({})}));
            }
        }

        void visit_index_operator(::HIR::ExprNode_Index& node, const ::HIR::TypeRef& ty_val, MIR::LValue value, const ::HIR::TypeRef& ty_idx, MIR::LValue index) {
            DEBUG("");
            const Span& sp = node.span();

            // NOTE: Do operator replacement here after handling scope-raising for _Borrow
            if (m_borrow_raise_target && m_in_borrow) {
                DEBUG("- Raising deref in borrow to scope " << *m_borrow_raise_target);
                m_builder.raise_temporaries(sp, value, *m_borrow_raise_target);
            }

            const char* langitem = nullptr;
            const char* method = nullptr;
            ::HIR::BorrowType bt;
            switch (node.m_value->m_usage) {
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
            pp_trait.m_types.push_back(ty_idx);
            ::HIR::GenericPath trait{m_builder.resolve().m_crate.get_lang_item_path(node.span(), langitem), std::move(pp_trait)};

            ::HIR::PathParams pp_method;
            pp_method.m_lifetimes.push_back(HIR::LifetimeRef());
            auto method_path = ::HIR::Path(ty_val, std::move(trait), RcString::new_interned(method), std::move(pp_method));

            // Store a borrow of the input value
            ::std::vector<::MIR::Param> args;
            args.push_back(m_builder.lvalue_or_temp(sp, m_builder.resolve().m_crate.m_types.borrow(bt, node.m_value->m_res_type), ::MIR::RValue::make_Borrow({bt, false, std::move(value)})));
            args.push_back(std::move(index));
            m_builder.moved_lvalue(node.span(), args[0].as_LValue());
            m_builder.moved_lvalue(node.span(), args[1].as_LValue());
            auto res_val = m_builder.new_temporary(m_builder.resolve().m_crate.m_types.borrow(bt, node.m_res_type));
            // Call the above trait method
            // Store result of that call in `val` (which will be derefed below)
            auto ok_block = m_builder.new_bb_unlinked();
            auto panic_block = m_builder.new_bb_unlinked();
            m_builder.end_block(::MIR::Terminator::make_Call({ok_block, panic_block, res_val.clone(), std::move(method_path), std::move(args)}));
            m_builder.set_cur_block(panic_block);
            emit_unwind(sp);

            m_builder.set_cur_block(ok_block);
            m_builder.set_result(node.span(), ::MIR::LValue::new_Deref(std::move(res_val)));
        }

        void visit(::HIR::ExprNode_Index& node) override {
            TRACE_FUNCTION_F("_Index");

            // NOTE: Calculate the index first (so if it borrows from the source, it's over by the time that's needed)
            const auto& ty_idx = node.m_index->m_res_type;
            this->visit_node_ptr(node.m_index);
            auto index = m_builder.get_result_in_lvalue(node.m_index->span(), ty_idx);

            const auto& ty_val = node.m_value->m_res_type;
            this->visit_node_ptr(node.m_value);
            auto value = m_builder.get_result_in_lvalue(node.m_value->span(), ty_val);

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
                    limit_val = ::MIR::RValue::make_DstMeta({m_builder.get_ptr_to_dst(node.m_value->span(), value)});
                }
            }

            {
                auto limit_lval = m_builder.lvalue_or_temp(node.span(), ty_idx, mv$(limit_val));

                auto cmp_res = m_builder.new_temporary(m_builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool));
                m_builder.push_stmt_assign(node.span(), cmp_res.clone(), ::MIR::RValue::make_BinOp({index.clone(), ::MIR::eBinOp::GE, limit_lval.clone()}));
                auto arm_panic = m_builder.new_bb_unlinked();
                auto arm_continue = m_builder.new_bb_unlinked();
                m_builder.end_block(::MIR::Terminator::make_If({mv$(cmp_res), arm_panic, arm_continue}));

                m_builder.set_cur_block(arm_panic);
                const auto& panic_bounds_check = m_builder.crate().get_lang_item_path(node.span(), "panic_bounds_check");
                auto panic_result = m_builder.new_temporary(m_builder.resolve().m_crate.m_types.diverge());
                auto panic_return = m_builder.new_bb_unlinked();
                auto panic_unwind = m_builder.new_bb_unlinked();
                m_builder.end_block(
                    ::MIR::Terminator::make_Call({
                        panic_return,
                        panic_unwind,
                        std::move(panic_result),
                        ::HIR::Path(panic_bounds_check),
                        make_vec2<::MIR::Param>(index.clone(), limit_lval.clone()),
                    })
                );

                m_builder.set_cur_block(panic_return);
                m_builder.end_block(::MIR::Terminator::make_Diverge({}));

                m_builder.set_cur_block(panic_unwind);
                emit_unwind(node.span());

                m_builder.set_cur_block(arm_continue);
            }

            if( !index.is_Local())
            {
                auto local_idx = m_builder.new_temporary(m_builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Usize));
                m_builder.push_stmt_assign(node.span(), local_idx.clone(), mv$(index));
                index = mv$(local_idx);
            }
            m_builder.set_result( node.span(), ::MIR::LValue::new_Index( mv$(value), index.m_root.as_Local() ) );
        }

        void visit(::HIR::ExprNode_Deref& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_Deref");

            const auto& ty_val = node.m_value->m_res_type;
            this->visit_node_ptr(node.m_value);
            auto val = m_builder.get_result_in_lvalue(node.m_value->span(), ty_val);

            bool use_trait = node.m_trait_used == ::HIR::ExprNode_Deref::TraitUsed::Trait;
            if (node.m_trait_used == ::HIR::ExprNode_Deref::TraitUsed::Unknown) {
                use_trait = !ty_val->is_Pointer() && !ty_val->is_Borrow() && !m_builder.is_type_owned_box(ty_val);
            }

            if (use_trait) {
                // Do operator replacement here after handling scope-raising
                // for _Borrow.  The type checker recorded this choice, so a
                // primitive reference can still dispatch to a user impl.
                if (m_borrow_raise_target && m_in_borrow) {
                    DEBUG("- Raising deref in borrow to scope " << *m_borrow_raise_target);
                    m_builder.raise_temporaries(node.span(), val, *m_borrow_raise_target);
                }

                const char* langitem = nullptr;
                const char* method = nullptr;
                ::HIR::BorrowType bt;
                // - Uses the value's usage beacuse for T: Copy node.m_value->m_usage is Borrow, but node.m_usage is Move
                switch (node.m_value->m_usage) {
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
                        TODO(sp, "ValueUsage::Move for desugared Deref of " << node.m_value->m_res_type);
                        break;
                }
                assert(langitem);
                assert(method);

                auto method_path = ::HIR::Path(ty_val, ::HIR::GenericPath(m_builder.resolve().m_crate.get_lang_item_path(node.span(), langitem), {}), method, HIR::PathParams(HIR::LifetimeRef()));

                ::std::vector<::MIR::Param> args;
                args.push_back(m_builder.lvalue_or_temp(sp, m_builder.resolve().m_crate.m_types.borrow(bt, node.m_value->m_res_type), ::MIR::RValue::make_Borrow({bt, false, mv$(val)})));
                m_builder.moved_lvalue(node.span(), args[0].as_LValue());
                val = m_builder.new_temporary(m_builder.resolve().m_crate.m_types.borrow(bt, node.m_res_type));
                auto ok_block = m_builder.new_bb_unlinked();
                auto panic_block = m_builder.new_bb_unlinked();
                m_builder.end_block(::MIR::Terminator::make_Call({ok_block, panic_block, val.clone(), mv$(method_path), mv$(args)}));
                m_builder.set_cur_block(panic_block);
                emit_unwind(sp);

                m_builder.set_cur_block(ok_block);
            }

            m_builder.set_result( node.span(), ::MIR::LValue::new_Deref( mv$(val) ) );
        }

        void visit(::HIR::ExprNode_Emplace& node) override {
            assert(node.m_type == ::HIR::ExprNode_Emplace::Type::Boxer);
            const auto& data_ty = node.m_value->m_res_type;

            node.m_value->visit(*this);
            auto val = m_builder.get_result(node.span());

            return box_new(node, data_ty, std::move(val));
        }

        void box_new(::HIR::ExprNode& node, const ::HIR::TypeRef& data_ty, ::MIR::RValue val) {
            const auto& lang_exchange_malloc = m_builder.crate().get_lang_item_path(node.span(), "exchange_malloc");
            //const auto& lang_owned_box = m_builder.crate().get_lang_item_path(node.span(), "owned_box");

            ::HIR::PathParams trait_params_data;
            trait_params_data.m_types.push_back(data_ty);
            auto& types = m_builder.resolve().m_crate.m_types;

            // 1. Determine the size/alignment of the type
            ::MIR::Param size_param, align_param;
            size_t item_size, item_align;
            if (Target_GetSizeAndAlignOf(node.span(), m_builder.resolve(), data_ty, item_size, item_align)) {
                size_param = ::MIR::Constant::make_Uint({U128(item_size), ::HIR::CoreType::Usize});
                align_param = ::MIR::Constant::make_Uint({U128(item_align), ::HIR::CoreType::Usize});
            } else {
                // Insert calls to "size_of" and "align_of" intrinsics
                auto size_slot = m_builder.new_temporary(types.primitive(::HIR::CoreType::Usize));
                auto size__panic = m_builder.new_bb_unlinked();
                auto size__ok = m_builder.new_bb_unlinked();
                m_builder.end_block(::MIR::Terminator::make_Call({size__ok, size__panic, size_slot.clone(), ::MIR::CallTarget::make_Intrinsic({"size_of", trait_params_data.clone()}), {}}));
                m_builder.set_cur_block(size__panic);
                emit_unwind(node.span());
                m_builder.set_cur_block(size__ok);
                auto align_slot = m_builder.new_temporary(types.primitive(::HIR::CoreType::Usize));
                auto align__panic = m_builder.new_bb_unlinked();
                auto align__ok = m_builder.new_bb_unlinked();
                m_builder.end_block(::MIR::Terminator::make_Call({align__ok, align__panic, align_slot.clone(), ::MIR::CallTarget::make_Intrinsic({"align_of", trait_params_data.clone()}), {}}));
                m_builder.set_cur_block(align__panic);
                emit_unwind(node.span());
                m_builder.set_cur_block(align__ok);

                size_param = ::std::move(size_slot);
                align_param = ::std::move(align_slot);
            }

            // 2. Call the allocator function and get a pointer
            // - NOTE: "exchange_malloc" returns a `*mut u8`, need to cast that to the target type
            auto place_raw_type = types.pointer(::HIR::BorrowType::Unique, types.primitive(::HIR::CoreType::U8));
            auto place_raw = m_builder.new_temporary(place_raw_type);

            auto place__panic = m_builder.new_bb_unlinked();
            auto place__ok = m_builder.new_bb_unlinked();
            m_builder.end_block(::MIR::Terminator::make_Call({place__ok, place__panic, place_raw.clone(), ::HIR::Path(lang_exchange_malloc), make_vec2<::MIR::Param>(::std::move(size_param), ::std::move(align_param))}));
            m_builder.set_cur_block(place__panic);
            emit_unwind(node.span());
            m_builder.set_cur_block(place__ok);

            auto place_type = types.pointer(::HIR::BorrowType::Unique, data_ty);
            auto place = m_builder.new_temporary(place_type);
            m_builder.push_stmt_assign(node.span(), place.clone(), ::MIR::RValue::make_Cast({mv$(place_raw), place_type}));
            // 3. Do a non-dropping write into the target location (i.e. just a MIR assignment)
            m_builder.push_stmt_assign(node.span(), ::MIR::LValue::new_Deref(place.clone()), mv$(val), /*drop_destination=*/false);
            // 4. Convert the pointer into an `owned_box`
            const auto& res_type = node.m_res_type;
            auto res = m_builder.new_temporary(res_type);
            auto cast__panic = m_builder.new_bb_unlinked();
            auto cast__ok = m_builder.new_bb_unlinked();
            ::HIR::PathParams transmute_params;
            transmute_params.m_types.push_back(res_type);
            transmute_params.m_types.push_back(place_type);
            m_builder.end_block(::MIR::Terminator::make_Call({cast__ok, cast__panic, res.clone(), ::MIR::CallTarget::make_Intrinsic({"transmute", mv$(transmute_params)}), make_vec1(::MIR::Param(mv$(place)))}));
            m_builder.set_cur_block(cast__panic);
            emit_unwind(node.span());
            m_builder.set_cur_block(cast__ok);

            m_builder.set_result(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNode_TupleVariant& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_TupleVariant");
            ::std::vector<::MIR::Param> values;
            values.reserve(node.m_args.size());
            for (auto& arg : node.m_args) {
                this->visit_node_ptr(arg);
                if (!m_builder.block_active()) {
                    return;
                }
                values.push_back(m_builder.get_result_in_param(arg->span(), arg->m_res_type));
            }

            if (node.m_is_struct) {
                m_builder.set_result(node.span(), ::MIR::RValue::make_Struct({node.m_path.clone(), mv$(values)}));
            } else {
                // Get the variant index from the enum.
                auto enum_path = node.m_path.clone();
                const auto var_name = enum_path.m_path.pop_component();
                const auto& enm = m_builder.crate().get_enum_by_path(sp, enum_path.m_path);

                size_t idx = enm.find_variant(var_name);
                ASSERT_BUG(sp, idx != SIZE_MAX, "Variant " << node.m_path.m_path << " isn't present");

                // TODO: Validation?
                ASSERT_BUG(sp, enm.m_data.is_Data(), "TupleVariant on non-data enum - " << node.m_path.m_path);


                m_builder.set_result(node.span(), ::MIR::RValue::make_EnumVariant({mv$(enum_path), static_cast<unsigned>(idx), mv$(values)}));
            }
        }

        ::std::vector<::MIR::Param> get_args(/*const*/ ::std::vector<::HIR::ExprNodeP>& args) {
            ::std::vector<::MIR::Param> values;
            values.reserve(args.size());
            for (auto& arg : args) {
                this->visit_node_ptr(arg);
                if (!m_builder.block_active()) {
                    return {};
                } else if (args.size() == 1) {
                    values.push_back(m_builder.get_result_in_param(arg->span(), arg->m_res_type, /*allow_missing_value=*/true));
                } else {
                    auto res = m_builder.get_result(arg->span());
                    if (auto* e = res.opt_Constant()) {
                        values.push_back(mv$(*e));
                    } else {
                        // NOTE: Have to allocate a new temporary because ordering matters
                        auto tmp = m_builder.new_temporary(arg->m_res_type);
                        m_builder.push_stmt_assign(arg->span(), tmp.clone(), mv$(res));
                        values.push_back(mv$(tmp));
                    }
                }
            }
            // Keep already evaluated arguments live while evaluating the remaining arguments.
            // A later argument can yield, so consuming an earlier temporary here would prevent
            // the coroutine lowering from saving a value that the eventual call still needs.
            for (size_t i = 0; i < values.size(); i++) {
                if (const auto* e = values[i].opt_LValue()) {
                    m_builder.moved_lvalue(args[i]->span(), *e);
                }
            }
            return values;
        }

        void visit(::HIR::ExprNode_CallPath& node) override {
            TRACE_FUNCTION_F("_CallPath " << node.m_path);
            // TODO: if this is a `<foo as Index[Mut]>::index[_mut]` call then allow the borrow raise to go through to the receiver
            ::std::vector<MIR::Param> values;
            bool is_operator = false;
            if (const auto* pe = node.m_path.m_data.opt_UfcsKnown()) {
                if (pe->trait.m_path == m_builder.resolve().m_crate.get_lang_item_path_opt("index")) {
                    is_operator = true;
                } else if (pe->trait.m_path == m_builder.resolve().m_crate.get_lang_item_path_opt("index_mut")) {
                    is_operator = true;
                } else if (pe->trait.m_path == m_builder.resolve().m_crate.get_lang_item_path_opt("deref")) {
                    is_operator = true;
                } else if (pe->trait.m_path == m_builder.resolve().m_crate.get_lang_item_path_opt("deref_mut")) {
                    is_operator = true;
                }
            }
            if (is_operator) {
                values = get_args(node.m_args);
            } else {
                auto _ = save_and_edit(m_borrow_raise_target, nullptr);
                values = get_args(node.m_args);
            }
            if (!m_builder.block_active()) {
                return;
            }

            auto panic_block = m_builder.new_bb_unlinked();
            auto next_block = m_builder.new_bb_unlinked();
            auto res = m_builder.new_temporary(node.m_res_type);

            bool unconditional_diverge = false;

            // Emit intrinsics as a special call type
            if (node.m_path.m_data.is_Generic()) {
                const auto& gpath = node.m_path.m_data.as_Generic();
                const auto& fcn = m_builder.crate().get_function_by_path(node.span(), gpath.m_path);
                if (gpath.m_path.crate_name() == "#intrinsics") {
                    const auto& name = gpath.m_path.components().back();
                    if (name == "offset_of") {
                        m_builder.end_block(::MIR::Terminator::make_Call({next_block, panic_block, res.clone(), ::MIR::CallTarget::make_Intrinsic({name, gpath.m_params.clone()}), mv$(values)}));
                    } else {
                        ERROR(node.span(), E0000, "Unknown builtin - " << gpath.m_path);
                    }
                } else if (fcn.m_abi == "rust-intrinsic") {
                    auto name = gpath.m_path.components().back();
                    if (name == "ptr_metadata") {
                        auto& v = values.front();
                        m_builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue::make_DstMeta({std::move(v.as_LValue())}));
                        m_builder.set_result(node.span(), std::move(res));
                        return;
                    }
                    // aggregate_raw_ptr: Lowers to mrustc's MakeDst (rustc's `Aggregate` with `AggregateKind::RawPtr`)
                    if (name == "aggregate_raw_ptr") {
                        auto& v_ptr = values.at(0);
                        auto& v_meta = values.at(1);
                        m_builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue::make_MakeDst({std::move(v_ptr), std::move(v_meta)}));
                        m_builder.set_result(node.span(), std::move(res));
                        return;
                    }
                    if (name == "ub_checks") {
                        m_builder.set_result(node.span(), ::MIR::Constant::make_Bool({true}));
                        return;
                    }
                    // `slice_get_unchecked`: Acts like `&mut foo[idx]`, but handles all inner types
                    if (name == "slice_get_unchecked") {
                        ::MIR::LValue slot;
                        TU_MATCH_HDRA((values[0]), {)
                        TU_ARMA(LValue, lv) {
                                slot = ::MIR::LValue::new_Deref(std::move(lv));
                            }
                            TU_ARMA(Constant, c) TODO(node.span(), "");
                            TU_ARMA(Borrow, v) {
                                slot = std::move(v.val);
                            }
                        }
                        ::MIR::LValue   index_lv = m_builder.new_temporary(m_builder.resolve().m_crate.m_types.primitive(HIR::CoreType::Usize));
                        TU_MATCH_HDRA((values[1]), {)
                        TU_ARMA(LValue, lv) {
                                m_builder.push_stmt_assign(node.span(), index_lv.clone(), std::move(lv));
                            }
                            TU_ARMA(Constant, c) {
                                m_builder.push_stmt_assign(node.span(), index_lv.clone(), std::move(c));
                            }
                            TU_ARMA(Borrow, v)
                            TODO(node.span(), "Borrow index?");
                        }
                        const auto& ptr_ty = gpath.m_params.m_types.at(0);
                        ASSERT_BUG(node.span(), ptr_ty->is_Borrow() || ptr_ty->is_Pointer(), "" << ptr_ty);
                        bool is_raw = ptr_ty->is_Pointer();
                        auto borrow_ty = is_raw ? ptr_ty->as_Pointer().type : ptr_ty->as_Borrow().type;
                        m_builder.push_stmt_assign(node.span(), res.clone(), ::MIR::RValue::make_Borrow({
                            borrow_ty,
                            is_raw,
                            ::MIR::LValue::new_Index(std::move(slot), std::move(index_lv.as_Local()))
                            }));
                        m_builder.set_result(node.span(), std::move(res));
                        return ;
                    }

                    // Floating point operations that can be algebraically optimised
                    // Lazy: Just conver to base operations
                    if (name == "fadd_algebraic") {
                        m_builder.set_result(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::ADD, std::move(values[1])}));
                        return;
                    }
                    if (name == "fsub_algebraic") {
                        m_builder.set_result(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::SUB, std::move(values[1])}));
                        return;
                    }
                    if (name == "fmul_algebraic") {
                        m_builder.set_result(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::MUL, std::move(values[1])}));
                        return;
                    }
                    if (name == "fdiv_algebraic") {
                        m_builder.set_result(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::DIV, std::move(values[1])}));
                        return;
                    }
                    if (name == "frem_algebraic") {
                        m_builder.set_result(node.span(), ::MIR::RValue::make_BinOp({std::move(values[0]), ::MIR::eBinOp::MOD, std::move(values[1])}));
                        return;
                    }
                    if (name == "box_new") {
                        // Call "exchange_malloc" and move the argument into that returned pointer (same as 1.29 emplace)
                        const auto& data_ty = gpath.m_params.m_types.at(0);
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
                        box_new(node, data_ty, std::move(val));
                        return ;
                    }
                    m_builder.end_block(::MIR::Terminator::make_Call({next_block, panic_block, res.clone(), ::MIR::CallTarget::make_Intrinsic({name, gpath.m_params.clone()}), mv$(values)}));
                } else if (fcn.m_abi == "platform-intrinsic") {
                    m_builder.end_block(::MIR::Terminator::make_Call({next_block, panic_block, res.clone(), ::MIR::CallTarget::make_Intrinsic({RcString(FMT("platform:" << gpath.m_path.components().back())), gpath.m_params.clone()}), mv$(values)}));
                }

                // rustc has drop_in_place as a lang item, mrustc uses an intrinsic
                if (gpath.m_path == m_builder.crate().get_lang_item_path_opt("drop_in_place")) {
                    m_builder.end_block(::MIR::Terminator::make_Call({next_block, panic_block, res.clone(), ::MIR::CallTarget::make_Intrinsic({"drop_in_place", gpath.m_params.clone()}), mv$(values)}));
                }

                if (fcn.m_return->is_Diverge()) {
                    unconditional_diverge = true;
                }
            } else {
                // TODO: Know if the call unconditionally diverges.
                if (node.m_cache.m_arg_types.back()->is_Diverge()) {
                    unconditional_diverge = true;
                }
            }

            // If the call wasn't to an intrinsic, emit it as a path
            if (m_builder.block_active()) {
                m_builder.end_block(::MIR::Terminator::make_Call({next_block, panic_block, res.clone(), node.m_path.clone(), mv$(values)}));
            }

            m_builder.set_cur_block(panic_block);
            emit_unwind(node.span());

            m_builder.set_cur_block(next_block);

            // If the function doesn't return, early-terminate the return block.
            if (unconditional_diverge) {
                m_builder.end_block(::MIR::Terminator::make_Diverge({}));
                m_builder.set_cur_block(m_builder.new_bb_unlinked());
            } else {
                // NOTE: This has to be done here because the builder can't easily do it.
                m_builder.mark_value_assigned(node.span(), res);
            }
            m_builder.set_result(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNode_CallValue& node) override {
            TRACE_FUNCTION_F("_CallValue " << node.m_value->m_res_type);
            auto _ = save_and_edit(m_borrow_raise_target, nullptr);

            // _CallValue is ONLY valid on function pointers (all others must be desugared)
            ASSERT_BUG(node.span(), node.m_value->m_res_type->is_Function(), "Leftover _CallValue on a non-fn()");
            this->visit_node_ptr(node.m_value);
            if (!m_builder.block_active()) {
                return;
            }

            // Get the function pointer in a temporary BEFORE getting arguments
            auto fcn_val = m_builder.new_temporary(node.m_value->m_res_type);
            m_builder.push_stmt_assign(node.m_value->span(), fcn_val.clone(), m_builder.get_result(node.m_value->span()));

            auto values = get_args(node.m_args);
            if (!m_builder.block_active()) {
                return;
            }

            auto panic_block = m_builder.new_bb_unlinked();
            auto next_block = m_builder.new_bb_unlinked();
            auto res = m_builder.new_temporary(node.m_res_type);
            m_builder.end_block(::MIR::Terminator::make_Call({next_block, panic_block, res.clone(), mv$(fcn_val), mv$(values)}));

            m_builder.set_cur_block(panic_block);
            emit_unwind(node.span());

            m_builder.set_cur_block(next_block);
            // TODO: Support diverging value calls
            m_builder.mark_value_assigned(node.span(), res);
            m_builder.set_result(node.span(), mv$(res));
        }

        void visit(::HIR::ExprNode_CallMethod& node) override {
            // TODO: Allow use on trait objects? May not be needed, depends.
            BUG(node.span(), "Leftover _CallMethod");
        }

        void visit(::HIR::ExprNode_Field& node) override {
            TRACE_FUNCTION_F("_Field \"" << node.m_field << "\"");
            this->visit_node_ptr(node.m_value);
            auto val = m_builder.get_result_in_lvalue(node.m_value->span(), node.m_value->m_res_type);

            const auto& val_ty = node.m_value->m_res_type;

            unsigned int idx;
            if (::std::isdigit(node.m_field.c_str()[0])) {
                ::std::stringstream(node.m_field.c_str()) >> idx;
                m_builder.set_result(node.span(), ::MIR::LValue::new_Field(mv$(val), idx));
            } else if (const auto* bep = val_ty->as_Path().binding.opt_Struct()) {
                const auto& str = **bep;
                const auto& fields = str.m_data.as_Named();
                idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == node.m_field;
                }) - fields.begin();
                m_builder.set_result(node.span(), ::MIR::LValue::new_Field(mv$(val), idx));
            } else if (const auto* bep = val_ty->as_Path().binding.opt_Union()) {
                const auto& unm = **bep;
                const auto& fields = unm.m_variants;
                idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == node.m_field;
                }) - fields.begin();

                m_builder.set_result(node.span(), ::MIR::LValue::new_Downcast(mv$(val), idx));
            } else {
                BUG(node.span(), "Field access on non-union/struct - " << val_ty);
            }
        }

        void visit(::HIR::ExprNode_Literal& node) override {
            TRACE_FUNCTION_F("_Literal");
            TU_MATCH_HDRA( (node.m_data), {)
            TU_ARMA(Integer, e) {
                    ASSERT_BUG(node.span(), node.m_res_type->is_Primitive(), "Non-primitive return type for Integer literal - " << node.m_res_type);
                    auto ity = node.m_res_type->as_Primitive();
                    switch (ity) {
                        case ::HIR::CoreType::U8:
                        case ::HIR::CoreType::U16:
                        case ::HIR::CoreType::U32:
                        case ::HIR::CoreType::U64:
                        case ::HIR::CoreType::U128:
                        case ::HIR::CoreType::Usize:
                            m_builder.set_result(node.span(), ::MIR::Constant::make_Uint({e.m_value, ity}));
                            break;
                        case ::HIR::CoreType::Char:
                            m_builder.set_result(node.span(), ::MIR::Constant::make_Uint({e.m_value, ity}));
                            break;
                        case ::HIR::CoreType::I8:
                        case ::HIR::CoreType::I16:
                        case ::HIR::CoreType::I32:
                        case ::HIR::CoreType::I64:
                        case ::HIR::CoreType::I128:
                        case ::HIR::CoreType::Isize:
                            m_builder.set_result(node.span(), ::MIR::Constant::make_Int({S128(e.m_value), ity}));
                            break;
                        default:
                            BUG(node.span(), "Integer literal with unexpected type - " << node.m_res_type);
                    }
                }
                TU_ARMA(Float, e) {
                    ASSERT_BUG(node.span(), node.m_res_type->is_Primitive(), "Non-primitive return type for Float literal - " << node.m_res_type);
                    auto ity = node.m_res_type->as_Primitive();
                    m_builder.set_result(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant::make_Float({e.m_value, ity})));
                }
                TU_ARMA(Boolean, e) {
                    m_builder.set_result(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant::make_Bool({e})));
                }
                TU_ARMA(String, e) {
                    m_builder.set_result(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant(e)));
                }
                TU_ARMA(CString, e) {
                    auto s = e.v;
                    s.push_back('\0');

                    // Emit as `transmute<&Cstr,&str>`
                    auto res = m_builder.new_temporary(node.m_res_type);

                    auto cast__panic = m_builder.new_bb_unlinked();
                    auto cast__ok = m_builder.new_bb_unlinked();
                    ::HIR::PathParams transmute_params;
                    transmute_params.m_types.push_back(node.m_res_type);
                    transmute_params.m_types.push_back(m_builder.resolve().m_crate.m_types.borrow(::HIR::BorrowType::Shared, m_builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Str)));
                    m_builder.end_block(::MIR::Terminator::make_Call({cast__ok, cast__panic, res.clone(), ::MIR::CallTarget::make_Intrinsic({"transmute", mv$(transmute_params)}), make_vec1(::MIR::Param(::MIR::Constant(std::move(s))))}));
                    m_builder.set_cur_block(cast__panic);
                    emit_unwind(node.span());
                    m_builder.set_cur_block(cast__ok);

                    m_builder.set_result(node.span(), mv$(res));
                }
                TU_ARMA(ByteString, e) {
                    auto v = mv$(*reinterpret_cast<::std::vector<uint8_t>*>(&e));
                    m_builder.set_result(node.span(), ::MIR::RValue::make_Constant(::MIR::Constant(mv$(v))));
                }
            }
        }

        void visit(::HIR::ExprNode_UnitVariant& node) override {
            const Span& sp = node.span();
            TRACE_FUNCTION_F("_UnitVariant");
            if (!node.m_is_struct) {
                // Get the variant index from the enum.
                auto enum_path = node.m_path.clone();
                auto var_name = enum_path.m_path.pop_component();

                const auto& enm = m_builder.crate().get_enum_by_path(sp, enum_path.m_path);

                auto idx = enm.find_variant(var_name);
                ASSERT_BUG(sp, idx != SIZE_MAX, "Variant " << node.m_path.m_path << " isn't present");

                // VALIDATION
                if (const auto* e = enm.m_data.opt_Data()) {
                    const auto& var = (*e)[idx];
                    ASSERT_BUG(sp, !var.is_struct, "Variant " << node.m_path.m_path << " isn't a unit variant");
                }

                m_builder.set_result(node.span(), ::MIR::RValue::make_EnumVariant({mv$(enum_path), static_cast<unsigned>(idx), {}}));
            } else {
                m_builder.set_result(node.span(), ::MIR::RValue::make_Struct({node.m_path.clone(), {}}));
            }
        }

        void visit(::HIR::ExprNode_PathValue& node) override {
            const auto& sp = node.span();
            TRACE_FUNCTION_F("_PathValue - " << node.m_path);
            if (node.m_res_type->is_NamedFunction() && node.m_target != ::HIR::ExprNode_PathValue::STATIC && node.m_target != ::HIR::ExprNode_PathValue::CONSTANT) {
                auto tmp = m_builder.new_temporary(node.m_res_type);
                m_builder.push_stmt_assign(sp, tmp.clone(), ::MIR::Constant::make_Function({box$(node.m_path.clone())}));
                //m_builder.push_stmt_assign( sp, tmp.clone(), ::MIR::Constant::make_ItemAddr({ box$(node.m_path.clone()) }) );
                m_builder.set_result(sp, mv$(tmp));
                return;
            }
            TU_MATCH_HDRA( (node.m_path.m_data), { )
            TU_ARMA(Generic, pe) {
                    // Enum variant constructor.
                    if (node.m_target == ::HIR::ExprNode_PathValue::ENUM_VAR_CONSTR) {
                        BUG(node.span(), "Should have produced a NamedFunction type and have been handled above");
                    }
                    const auto& vi = m_builder.crate().get_valitem_by_path(node.span(), pe.m_path);
                TU_MATCH_HDRA( (vi), {)
                TU_ARMA(Import, e) {
                            BUG(sp, "All references via imports should be replaced");
                        }
                        TU_ARMA(Constant, e) {
                            auto ty = MonomorphStatePtr(m_builder.resolve().m_crate.m_types, nullptr, nullptr, &pe.m_params).monomorph_type(sp, e.m_type);
                            auto tmp = m_builder.new_temporary(ty);
                            m_builder.push_stmt_assign(sp, tmp.clone(), ::MIR::Constant::make_Const({box$(node.m_path.clone())}));
                            m_builder.set_result(node.span(), mv$(tmp));
                        }
                        TU_ARMA(Static, e) {
                            m_builder.set_result(node.span(), ::MIR::LValue::new_Static(node.m_path.clone()));
                        }
                        TU_ARMA(StructConstant, e) {
                            // TODO: Why is this still a PathValue?
                            m_builder.set_result(node.span(), ::MIR::RValue::make_Struct({pe.clone(), {}}));
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
                    const auto& tr = m_builder.crate().get_trait_by_path(sp, pe.trait.m_path);
                    auto it = tr.m_values.find(pe.item);
                    ASSERT_BUG(sp, it != tr.m_values.end(), "Cannot find trait item for " << node.m_path);
                    TU_MATCHA((it->second), (e), (Constant, m_builder.set_result(sp, ::MIR::Constant::make_Const({box$(node.m_path.clone())}));), (Static, TODO(sp, "Associated statics (non-rustc) - " << node.m_path);), (Function, BUG(node.span(), "Should have produced a NamedFunction type and have been handled above");))
                }
                TU_ARMA(UfcsUnknown, pe) {
                    BUG(sp, "PathValue - Encountered UfcsUnknown - " << node.m_path);
                }
                TU_ARMA(UfcsInherent, pe) {
                    // 1. Find item in an impl block
                    auto rv = m_builder.crate().find_type_impls(pe.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                        DEBUG("- impl" << impl.m_params.fmt_args() << " " << impl.m_type);
                        // Associated functions
                        {
                            auto it = impl.m_methods.find(pe.item);
                            if (it != impl.m_methods.end()) {
                                //BUG(node.span(), "Should have produced a NamedFunction type and have been handled above: ");
                                m_builder.set_result(sp, ::MIR::Constant::make_ItemAddr({box$(node.m_path.clone())}));
                                return true;
                            }
                        }
                        // Associated consts
                        {
                            auto it = impl.m_constants.find(pe.item);
                            if (it != impl.m_constants.end()) {
                                m_builder.set_result(sp, ::MIR::Constant::make_Const({box$(node.m_path.clone())}));
                                return true;
                            }
                        }
                        // Associated static (undef)
                        return false;
                    });
                    if (!rv) {
                        ERROR(sp, E0000, "Failed to locate item for " << node.m_path);
                    }
                }
            }
        }

        void visit(::HIR::ExprNode_Variable& node) override {
            TRACE_FUNCTION_F("_Variable - " << node.m_name << " #" << node.m_slot);
#if 1
            // If there's an alias active, emit that
            if (const auto* a = m_builder.get_variable_alias(node.span(), node.m_slot)) {
                switch (a->first) {
                    case ::HIR::PatternBinding::Type::Move:
                        m_builder.set_result(node.span(), a->second.clone());
                        break;
                    case ::HIR::PatternBinding::Type::Ref:
                        m_builder.set_result(node.span(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, a->second.clone()}));
                        break;
                    case ::HIR::PatternBinding::Type::MutRef:
                        m_builder.set_result(node.span(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Unique, false, a->second.clone()}));
                        break;
                }
                return;
            }
#endif
            m_builder.set_result(node.span(), m_builder.get_variable(node.span(), node.m_slot));
        }

        void visit(::HIR::ExprNode_ConstParam& node) override {
            TRACE_FUNCTION_F("_ConstParam - " << node.m_name << " #" << node.m_binding);
            m_builder.set_result(node.span(), ::MIR::Constant::make_Generic({node.m_name, node.m_binding}));
        }

        void visit_sl_inner(::HIR::ExprNode_StructLiteral& node, const ::HIR::Struct& str, const ::HIR::GenericPath& path) {
            const Span& sp = node.span();

            ASSERT_BUG(sp, str.m_data.is_Named(), "");
            const ::HIR::t_struct_fields& fields = str.m_data.as_Named();

            ::std::vector<bool> values_set;
            ::std::vector<::MIR::Param> values;
            values.resize(fields.size());
            values_set.resize(fields.size());

            for (auto& ent : node.m_values) {
                auto& valnode = ent.second;
                auto idx = ::std::find_if(fields.begin(), fields.end(), [&](const auto& x) {
                    return x.name == ent.first;
                }) - fields.begin();
                assert(!values_set[idx]);
                values_set[idx] = true;
                DEBUG("_StructLiteral - fld '" << ent.first << "' (idx " << idx << ")");
                this->visit_node_ptr(valnode);
                if (!m_builder.block_active()) {
                    return;
                }

                auto res = m_builder.get_result(valnode->span());
                if (auto* e = res.opt_Constant()) {
                    values.at(idx) = mv$(*e);
                } else {
                    // NOTE: Have to allocate a new temporary because ordering matters
                    auto tmp = m_builder.new_temporary(valnode->m_res_type);
                    m_builder.push_stmt_assign(valnode->span(), tmp.clone(), mv$(res));
                    values.at(idx) = mv$(tmp);
                }
            }

            auto base_val = ::MIR::LValue::new_Return();
            if (node.m_base_value) {
                DEBUG("_StructLiteral - base");
                this->visit_node_ptr(node.m_base_value);
                if (!m_builder.block_active()) {
                    return;
                }
                base_val = m_builder.get_result_in_lvalue(node.m_base_value->span(), node.m_base_value->m_res_type);
            }
            for (unsigned int i = 0; i < values.size(); i++) {
                if (!values_set[i]) {
                    if (node.m_base_value) {
                        values[i] = ::MIR::LValue::new_Field(base_val.clone(), i);
                    } else if (fields[i].default_value) {
                        const auto& v = *fields[i].default_value;
                        auto ms = MonomorphStatePtr(m_builder.resolve().m_crate.m_types, nullptr, &path.m_params, nullptr);
                        values[i] = m_builder.lvalue_or_temp(sp, ms.monomorph_type(sp, fields[i].ty), MIR::Constant::make_Const({::std::make_unique<HIR::Path>(ms.monomorph_genericpath(sp, v))}));
                    } else {
                        ERROR(node.span(), E0000, "Field '" << fields[i].name << "' not specified");
                    }
                } else {
                    // Partial move support will handle dropping the rest?
                }
            }

            m_builder.set_result(node.span(), ::MIR::RValue::make_Struct({path.clone(), mv$(values)}));
        }

        void visit(::HIR::ExprNode_StructLiteral& node) override {
            TRACE_FUNCTION_F("_StructLiteral");

            const auto& ty_path = node.m_real_path;

            TU_MATCH_HDRA( (node.m_res_type->as_Path().binding), {)
            TU_ARMA(Unbound, _e) {
                }
                TU_ARMA(Opaque, _e) {
                }
                TU_ARMA(Enum, e) {
                    auto enum_path = ty_path.clone();
                    auto var_name = enum_path.m_path.pop_component();

                    const auto& enm = *e;
                    size_t idx = enm.find_variant(var_name);
                    ASSERT_BUG(node.span(), idx != SIZE_MAX, "");
                    ASSERT_BUG(node.span(), enm.m_data.is_Data(), "");
                    const auto& var_ty = enm.m_data.as_Data()[idx].type;
                    const auto& str = *var_ty->as_Path().binding.as_Struct();

                    // Take advantage of the identical generics to cheaply clone/monomorph the path.
                    ::HIR::GenericPath struct_path = ty_path.clone();
                    struct_path.m_path = var_ty->as_Path().path.m_data.as_Generic().m_path;

                    this->visit_sl_inner(node, str, struct_path);
                    if (!m_builder.block_active()) {
                        return;
                    }
                    auto vals = std::move(m_builder.get_result(node.span()).as_Struct().vals);

                    // And create Variant
                    m_builder.set_result(node.span(), ::MIR::RValue::make_EnumVariant({mv$(enum_path), static_cast<unsigned>(idx), mv$(vals)}));
                }
                TU_ARMA(Union, e) {
                    const auto& variant_name = node.m_values.front().first;
                    auto& value_node = node.m_values.front().second;
                    this->visit_node_ptr(value_node);
                    if (!m_builder.block_active()) {
                        return;
                    }
                    auto val = m_builder.get_result_in_lvalue(value_node->span(), value_node->m_res_type);

                    const auto& unm = *e;
                    auto it = ::std::find_if(unm.m_variants.begin(), unm.m_variants.end(), [&](const HIR::StructField& v) -> auto {
                        return v.name == variant_name;
                    });
                    assert(it != unm.m_variants.end());
                    unsigned int idx = it - unm.m_variants.begin();

                    m_builder.set_result(node.span(), ::MIR::RValue::make_UnionVariant({node.m_real_path.clone(), idx, mv$(val)}));
                }
                TU_ARMA(ExternType, e) {
                    BUG(node.span(), "_StructLiteral ExternType isn't valid?");
                }
                TU_ARMA(Struct, e) {
                    if (e->m_data.is_Unit()) {
                        m_builder.set_result(node.span(), ::MIR::RValue::make_Struct({ty_path.clone(), {}}));
                        return;
                    }

                    this->visit_sl_inner(node, *e, ty_path);
                }
            }
        }

        void visit(::HIR::ExprNode_Tuple& node) override {
            TRACE_FUNCTION_F("_Tuple");
            auto values = get_args(node.m_vals);
            if (!m_builder.block_active()) {
                return;
            }

            m_builder.set_result(node.span(), ::MIR::RValue::make_Tuple({mv$(values)}));
        }

        void visit(::HIR::ExprNode_ArrayList& node) override {
            TRACE_FUNCTION_F("_ArrayList");
            auto values = get_args(node.m_vals);
            if (!m_builder.block_active()) {
                return;
            }

            m_builder.set_result(node.span(), ::MIR::RValue::make_Array({mv$(values)}));
        }

        void visit(::HIR::ExprNode_ArraySized& node) override {
            TRACE_FUNCTION_F("_ArraySized");
            this->visit_node_ptr(node.m_val);
            if (!m_builder.block_active()) {
                return;
            }
            auto value = m_builder.get_result_in_param(node.span(), node.m_val->m_res_type);

            m_builder.set_result(node.span(), ::MIR::RValue::make_SizedArray({mv$(value), std::move(node.m_size)}));
            // Ensure that the size is valid (avoids crashes when debug is enabled)
            node.m_size = HIR::ArraySize();
        }

        void visit(::HIR::ExprNode_Closure& node) override {
            TRACE_FUNCTION_F("_Closure - " << node.m_obj_path);
            auto _ = save_and_edit(m_borrow_raise_target, nullptr);

            ::std::vector<::MIR::Param> vals;
            vals.reserve(node.m_captures.size());
            for (auto& arg : node.m_captures) {
                this->visit_node_ptr(arg);
                vals.push_back(m_builder.get_result_in_lvalue(arg->span(), arg->m_res_type));
            }

            m_builder.set_result(node.span(), ::MIR::RValue::make_Struct({node.m_obj_path.clone(), mv$(vals)}));
        }

        void visit_common_cr(const Span& sp, const HIR::GenericPath& obj_path, const HIR::TypeRef& state_type, ::std::vector<::HIR::ExprNodeP>& captures) {
            auto _ = save_and_edit(m_borrow_raise_target, nullptr);

            ::std::vector<::MIR::Param> vals;
            vals.reserve(1 + captures.size());

            // Zero the state index
            {
                const auto& lang_MaybeUninit = m_builder.resolve().m_crate.get_lang_item_path(sp, "maybe_uninit");
                const auto& unm_MaybeUninit = m_builder.resolve().m_crate.get_union_by_path(sp, lang_MaybeUninit);
                auto slot_type = m_builder.resolve().m_crate.m_types.path(::HIR::GenericPath(lang_MaybeUninit, ::HIR::PathParams(state_type)), &unm_MaybeUninit);

                auto res_slot = m_builder.new_temporary(slot_type);
                auto size__panic = m_builder.new_bb_unlinked();
                auto size__ok = m_builder.new_bb_unlinked();
                m_builder.end_block(
                    ::MIR::Terminator::make_Call(
                        {size__ok,
                         size__panic,
                         res_slot.clone(),
                         ::MIR::CallTarget::make_Intrinsic({"init", ::HIR::PathParams(mv$(slot_type))}), // I.e. `mem::zeroed`
                         {}}
                    )
                );
                m_builder.set_cur_block(size__panic);
                emit_unwind(sp);
                m_builder.set_cur_block(size__ok);
                vals.push_back(std::move(res_slot));
            }
            // Populate the rest
            for (auto& arg : captures) {
                this->visit_node_ptr(arg);
                vals.push_back(m_builder.get_result_in_lvalue(arg->span(), arg->m_res_type));
            }

            m_builder.set_result(sp, ::MIR::RValue::make_Struct({obj_path.clone(), mv$(vals)}));
        }

        void visit(::HIR::ExprNode_Generator& node) override {
            TRACE_FUNCTION_F("_Generator - " << node.m_obj_path);
            ASSERT_BUG(node.span(), node.m_obj_ptr, "Generator not created");
            ASSERT_BUG(node.span(), !node.m_code, "Encountered outer generator wrapper");

            visit_common_cr(node.span(), node.m_obj_path, node.m_state_data_type, node.m_captures);
        }

        void visit(::HIR::ExprNode_GeneratorWrapper& node) override {
            BUG(node.span(), "Unexpected");
        }

        void visit(::HIR::ExprNode_AsyncBlock& node) override {
            TRACE_FUNCTION_F("_AsyncBlock - " << node.m_obj_path);
            ASSERT_BUG(node.span(), node.m_obj_ptr, "Future not created");
            ASSERT_BUG(node.span(), !node.m_code, "Encountered code inside post-expand async block");

            visit_common_cr(node.span(), node.m_obj_path, node.m_state_data_type, node.m_captures);
        }
    };
}

::MIR::FunctionPointer LowerMIR(const StaticTraitResolve& resolve, const ::HIR::ItemPath& path, const ::HIR::ExprPtr& ptr, const ::HIR::TypeRef& ret_ty, const ::HIR::Function::args_t& args) {
    TRACE_FUNCTION_F(path);

    ::MIR::Function fcn;
    fcn.locals.reserve(ptr.m_bindings.size());
    for (const auto& t : ptr.m_bindings) {
        fcn.locals.push_back(t);
    }

    // Scope ensures that builder cleanup happens before `fcn` is moved
    {
        const Span& sp = ptr->span();

        ::HIR::ExprNode& root_node = const_cast<::HIR::ExprNode&>(*ptr);
        MirBuilder builder{ptr->span(), resolve, ret_ty, args, fcn};
        ExprVisitor_Conv ev{builder, ptr.m_bindings, dynamic_cast<::HIR::ExprNode_GeneratorWrapper*>(&root_node)};

        // 1. Apply destructuring to arguments
        unsigned int i = 0;
        for (const auto& arg : args) {
            const auto& pat = arg.first;
            // If the binding is set (i.e. this isn't destructuring) then the table populated by `MirBuilder::MirBuilder(...)` will be used
            if (pat.m_bindings.size() == 1 && pat.m_bindings[0].m_type == ::HIR::PatternBinding::Type::Move && pat.m_data.is_Any()) {
                // Simple `var: Type` arguments are handled by `MirBuilder.m_var_arg_mappings`
            } else {
                DEBUG("Argument a" << i << " - " << pat);
                ev.define_vars_from(ptr->span(), arg.first);
                MIR_LowerHIR_Let(builder, ev, ptr->span(), arg.first, ::MIR::LValue::new_Argument(i), /*else_node=*/nullptr);
            }
            i++;
        }

        // 2. Destructure code
        if (auto* gen_node = dynamic_cast<::HIR::ExprNode_GeneratorWrapper*>(&root_node)) {
            // Mark all capture locals as valid (for later rewrite into variable acesses)
            ::std::map<unsigned, std::vector<MIR::LValue::Wrapper>> mappings;
            for (size_t i = 0; i < gen_node->m_capture_usages.size(); i++) {
                unsigned idx = args.size() + i;
                builder.define_variable(idx);
                switch (gen_node->m_capture_usages[i]) {
                    case ::HIR::ValueUsage::Borrow:
                    case ::HIR::ValueUsage::Mutate: {
                        // TODO: Use `m_variable_aliases` for by-borrow captures, to avoid them being dropped
                        auto lv = ::MIR::LValue::new_Argument(0);
                        lv.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Field(0)); // Pin.ptr
                        lv.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Deref());  // *
                        lv.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Field(1 + i));
                        lv.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Deref());
                        builder.add_variable_alias(root_node.span(), idx, ::HIR::PatternBinding::Type::Move, std::move(lv));
                    } break;
                    case ::HIR::ValueUsage::Move:
                    case ::HIR::ValueUsage::Unknown:
                        builder.mark_value_assigned(root_node.span(), ::MIR::LValue::new_Local(idx));
                        mappings.insert(std::make_pair(idx, ::make_vec1(::MIR::LValue::Wrapper::new_Field(1 + i))));
                        break;
                }
            }

            // ------------

            gen_node->m_code->visit(ev);
            if (builder.block_active() && builder.has_result()) {
                ev.coroutine_return(sp, gen_node->m_code->m_res_type);
            }
            builder.final_cleanup();

            // ------------

            // 1. Generate the state machine switch (and enumerate saved variables)
            std::set<unsigned> saved = ev.generator_finalise(gen_node->span(), const_cast<HIR::Enum&>(resolve.m_crate.get_enum_by_path(sp, gen_node->m_state_idx_enum)));
            // 2. Populate state structure
            auto& state_ty = const_cast<HIR::Struct&>(*gen_node->m_state_data_type->as_Path().binding.as_Struct());
            unsigned value_var_idx;
            {
                const auto& unm_MaybeUninit = resolve.m_crate.get_union_by_path(sp, resolve.m_crate.get_lang_item_path(gen_node->span(), "maybe_uninit"));
                value_var_idx = std::find_if(unm_MaybeUninit.m_variants.begin(), unm_MaybeUninit.m_variants.end(), [&](const auto& e) {
                    return e.name == "value";
                }) - unm_MaybeUninit.m_variants.begin();
            }
            ASSERT_BUG(sp, value_var_idx == 1, "Assumption on MaybeUninit.value's variant index failed");
            // - Any variables that are saved twice need to have a static address, others can share?
            // - Lazy option (doesn't require making sub-types): Toss everything together
            auto& fields = state_ty.m_data.as_Tuple();
            for (auto idx : saved) {
                if (idx < 1 + gen_node->m_capture_usages.size()) {
                } else {
                    auto field_idx = fields.size();
                    ASSERT_BUG(sp, idx < fcn.locals.size(), idx << " >= " << fcn.locals.size());
                    fields.push_back(::HIR::VisEnt<HIR::TypeRef>{HIR::Publicity::new_none(), fcn.locals.at(idx)});
                    // self.state(0).value(?#1).value(?0).IDX
                    mappings.insert(
                        std::make_pair(
                            idx,
                            std::vector<MIR::LValue::Wrapper>{
                                ::MIR::LValue::Wrapper::new_Field(0),
                                ::MIR::LValue::Wrapper::new_Downcast(value_var_idx), // MaybeUninit.value
                                ::MIR::LValue::Wrapper::new_Field(0),                // ManuallyDrop.value
                                ::MIR::LValue::Wrapper::new_Field(field_idx)
                            }
                        )
                    );
                }
            }
            for (const auto& m : mappings) {
                DEBUG("Mapping _" << m.first << " = " << m.second);
            }
            ::std::map<unsigned, unsigned> drop_flag_mapping;
            for (auto idx : ev.generator_drop_flags()) {
                drop_flag_mapping[idx] = drop_flag_mapping.size();
                DEBUG("df$" << idx << " = BIT" << drop_flag_mapping[idx]);
            }
            // Add drop flags to the end
            auto drop_flags_field_idx = fields.size();
            fields.push_back(::HIR::VisEnt<HIR::TypeRef>{HIR::Publicity::new_none(), resolve.m_crate.m_types.array(resolve.m_crate.m_types.primitive(::HIR::CoreType::U8), (drop_flag_mapping.size() + 7) / 8)});

            // 3. Rewrite usage of saved values
            // - Note: Need to allocate new temporaries if indexing by an updated lvalue
            class Rewriter: public ::MIR::visit::VisitorMut {
                /// Remapped locals (indexes into coroutine struct, not just into the state)
                ///
                /// From `Pin<&mut self>`, these are appended to `self.pin.*`
                const ::std::map<unsigned, std::vector<MIR::LValue::Wrapper>>& m_mappings;
                /// Mapping from drop flag indexes to bit sin the drop flag list
                const ::std::map<unsigned, unsigned>& m_drop_flag_mapping;
                /// Index of the drop flags bitset (array of u8) in the state (field 0 of top structure)
                unsigned m_drop_flags_field;

                ::std::vector<::MIR::Statement> m_new_statements;
                unsigned bb_idx = 0;
                unsigned stmt_idx = 0;

            public:
                Rewriter(const ::std::map<unsigned, std::vector<MIR::LValue::Wrapper>>& mappings, const ::std::map<unsigned, unsigned>& drop_flag_mapping, unsigned drop_flags_field)
                    : m_mappings(mappings)
                    , m_drop_flag_mapping(drop_flag_mapping)
                    , m_drop_flags_field(drop_flags_field)
                {
                }

                bool visit_lvalue(::MIR::LValue& lv, ::MIR::visit::ValUsage u) override {
                    if (lv.m_root.is_Local()) {
                        auto it = m_mappings.find(lv.m_root.as_Local());
                        if (it != m_mappings.end()) {
                            lv.m_root = ::MIR::LValue::Storage::new_Argument(0);
                            auto dit = lv.m_wrappers.begin();
                            dit = lv.m_wrappers.insert(dit, ::MIR::LValue::Wrapper::new_Field(0)) + 1; // Pin.ptr
                            dit = lv.m_wrappers.insert(dit, ::MIR::LValue::Wrapper::new_Deref()) + 1;  // *
                            dit = lv.m_wrappers.insert(dit, it->second.begin(), it->second.end()) + 1;
                            DEBUG("BB" << bb_idx << "/" << FMT_CB(os, if (stmt_idx == ~0u) { os << "TERM"; } else { os << stmt_idx; }) << " > " << lv);
                        }
                    }
                    for (auto& w : lv.m_wrappers) {
                        if (w.is_Index()) {
                            auto it = m_mappings.find(w.as_Index());
                            if (it != m_mappings.end()) {
                                // Allocate a new temporary, assign it before this statement, use that
                                TODO(Span(), "");
                            }
                        }
                    }

                    return true;
                }

                bool visit_stmt(::MIR::Statement& stmt) override {
                    auto get_drop_flags_slot = [this]() -> MIR::LValue {
                        ::MIR::LValue slot = ::MIR::LValue::new_Argument(0);
                        slot.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Field(0));                  // Pin.ptr
                        slot.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Deref());                   // *
                        slot.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Field(0));                  // .0
                        slot.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Downcast(1));               // .value (From MaybeUninit)
                        slot.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Field(0));                  // .value (From ManuallyDrop)
                        slot.m_wrappers.push_back(::MIR::LValue::Wrapper::new_Field(m_drop_flags_field)); // .drop_flags
                        return slot;
                    };
                    if (auto* s = stmt.opt_Drop()) {
                        if (m_drop_flag_mapping.count(s->flag_idx) != 0) {
                            // `LoadDropFlag(df$N, src_lv, bit_num)`, where `src_lv` is an array of `u8`
                            auto slot = get_drop_flags_slot();
                            unsigned bit_num = m_drop_flag_mapping.at(s->flag_idx);
                            m_new_statements.push_back(
                                ::MIR::Statement::make_LoadDropFlag({
                                    s->flag_idx,
                                    std::move(slot),
                                    bit_num,
                                })
                            );
                        }
                    } else if (auto* s = stmt.opt_SetDropFlag()) {
                        if (m_drop_flag_mapping.count(s->other) != 0) {
                            auto slot = get_drop_flags_slot();
                            unsigned bit_num = m_drop_flag_mapping.at(s->other);
                            // `LoadDropFlag(df$N, src_lv, bit_num)`, where `src_lv` is an array of `u8`
                            m_new_statements.push_back(
                                ::MIR::Statement::make_LoadDropFlag({
                                    s->other,
                                    std::move(slot),
                                    bit_num,
                                })
                            );
                        }
                        if (m_drop_flag_mapping.count(s->idx) != 0) {
                            // Copy this statement to the output queue, and then rewrite to be:
                            m_new_statements.push_back(*s);
                            // `SaveDropFlag(dst_lv, bit_num, df$N)`
                            auto slot = get_drop_flags_slot();
                            unsigned bit_num = m_drop_flag_mapping.at(s->idx);
                            stmt = ::MIR::Statement::make_SaveDropFlag({std::move(slot), bit_num, s->idx});
                            // TODO: Replace with no-op? (or let it be cleaned up later as dead code)
                        }
                    } else {
                        // Doesn't use drop flags, no changes/rewrites needed
                    }
                    return ::MIR::visit::VisitorMut::visit_stmt(stmt);
                }

                void push_statements(::MIR::BasicBlock& bb, size_t& ofs) {
                    for (auto& e : m_new_statements) {
                        bb.statements.insert(bb.statements.begin() + ofs, std::move(e));
                        ofs += 1;
                    }
                    m_new_statements.clear();
                }

                void rewrite_fcn(::MIR::Function& f) {
                    for (auto& bb : f.blocks) {
                        this->bb_idx = &bb - f.blocks.data();
                        for (size_t stmt_idx = 0; stmt_idx < bb.statements.size(); stmt_idx++) {
                            this->stmt_idx = stmt_idx;
                            this->visit_stmt(bb.statements[stmt_idx]);
                            this->push_statements(bb, stmt_idx);
                        }
                        this->stmt_idx = ~0u;
                        this->visit_terminator(bb.terminator);
                        size_t stmt_idx = bb.statements.size();
                        this->push_statements(bb, stmt_idx);
                    }
                }
            };

            Rewriter(mappings, drop_flag_mapping, drop_flags_field_idx).rewrite_fcn(fcn);

            // 4. Generate drop glue for the generator type and save for later
            // - Make a builder
            // - Insert the switch for each arm
            // - Trigger drops
            auto drop_impl_body = ::MIR::FunctionPointer(new ::MIR::Function());
            {
                TRACE_FUNCTION_F("Generating drop impl");
                MirBuilder drop_builder(sp, resolve, resolve.m_crate.m_types.unit(), gen_node->m_drop_fcn_ptr->m_args, *drop_impl_body);
                ev.generator_make_drop(sp, drop_builder, gen_node->m_capture_usages.size(), mappings, drop_flags_field_idx, drop_flag_mapping);
                drop_builder.final_cleanup();
            }
            for (auto& bb : drop_impl_body->blocks) {
                for (auto& stmt : bb.statements) {
                    if (auto* d = stmt.opt_Drop()) {
                        if (d->flag_idx != ~0u) {
                            d->flag_idx = drop_flag_mapping.at(d->flag_idx);
                        }
                    }
                    if (auto* d = stmt.opt_LoadDropFlag()) {
                        d->idx = drop_flag_mapping.at(d->idx);
                    }
                }
            }
            MIR_Validate(resolve, path, *drop_impl_body, gen_node->m_drop_fcn_ptr->m_args, resolve.m_crate.m_types.unit());
            gen_node->m_drop_fcn_ptr->m_code.m_mir = std::move(drop_impl_body);
        } else {
            root_node.visit(ev);
            builder.final_cleanup();
        }
    }

    // NOTE: Can't clean up yet, as consteval isn't done
    //MIR_Cleanup(resolve, path, fcn, args, ret_ty);
    //DEBUG("MIR Dump:" << ::std::endl << FMT_CB(ss, MIR_Dump_Fcn(ss, fcn, 1);));
    MIR_Validate(resolve, path, fcn, args, ret_ty);

    if (getenv("MRUSTC_VALIDATE_FULL_EARLY")) {
        MIR_Validate_Full(resolve, path, fcn, args, ptr->m_res_type);
    }

    return ::MIR::FunctionPointer(new ::MIR::Function(mv$(fcn)));
}

// --------------------------------------------------------------------

void HIR_GenerateMIR_Expr(const ::HIR::Crate& crate, const ::HIR::ItemPath& path, ::HIR::ExprPtr& expr_ptr, const ::HIR::Function::args_t& args, const ::HIR::TypeRef& res_ty) {
    if (!expr_ptr.m_mir) {
        TRACE_FUNCTION;
        StaticTraitResolve resolve{crate};
        resolve.set_both_generics_raw(expr_ptr.m_state->m_impl_generics, expr_ptr.m_state->m_item_generics);
        expr_ptr.set_mir(LowerMIR(resolve, path, expr_ptr, res_ty, args));
        // Run cleanup to simplify consteval?
        // - This ends up running before things like vtable generation, so parts of cleanup won't work.
        //MIR_Cleanup(resolve, path, *expr_ptr.m_mir, args, res_ty);
        // This path prepares an on-demand body for the constant evaluator, not
        // the runtime MIR selected by the driver. Keep normal inlining disabled,
        // but retain the local simplification that CTFE historically required.
        MIR_Optimise(resolve, path, *expr_ptr.m_mir, args, res_ty, /*opt_level=*/2, /*do_inline=*/false);
    }
}

void HIR_GenerateMIR(::HIR::Crate& crate) {
    ::MIR::OuterVisitor ov{crate, [&](const auto& res, const auto& p, ::HIR::ExprPtr& expr_ptr, const auto& args, const auto& ty) {
        if (!expr_ptr.get_mir_opt()) {
            expr_ptr.set_mir(LowerMIR(res, p, expr_ptr, ty, args));
        }
    }};
    ov.visit_crate(crate);
}

#include "mir_from_hir.h"
#include "hir_typeck_common.h" // monomorphise_type
#include <algorithm>
#include <numeric>
#include <limits> // std::numeric_limits
#include "trans_target.h"
#include "hir_conv_main_bindings.h" // For consteval

void MIR_LowerHIR_Match(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNode_Match& node, ::MIR::LValue match_val);

namespace {
    void get_ty_and_val(
        const Span& sp,
        MirBuilder& builder,
        const ::HIR::TypeRef& top_ty,
        const ::MIR::LValue& top_val,
        const field_path_t& field_path,
        unsigned int field_path_ofs,
        /*Out ->*/ ::HIR::TypeRef& out_ty,
        ::MIR::LValue& out_val
    );
}

void MIR_LowerHIR_GetTypeValueForPath(
    const Span& sp,
    MirBuilder& builder,
    const ::HIR::TypeRef& top_ty,
    const ::MIR::LValue& top_val,
    const field_path_t& field_path,
    /*Out ->*/ ::HIR::TypeRef& out_ty,
    ::MIR::LValue& out_val
) {
    get_ty_and_val(sp, builder, top_ty, top_val, field_path, 0, out_ty, out_val);
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
    (field_path_t field_path;

     bool operator<(const PatternRule & x) const { return this->ord(x) == OrdLess; } bool operator==(const PatternRule & x) const { return this->ord(x) == OrdEqual; } bool operator!=(const PatternRule & x) const { return this->ord(x) != OrdEqual; } Ordering ord(const PatternRule& x) const;
     PatternRule clone() const;)
);
::std::ostream& operator<<(::std::ostream& os, const PatternRule& x);

/// Constructed set of rules from a pattern
struct PatternRuleset {
    unsigned int arm_idx;
    unsigned int arm_rule_idx;

    ::std::vector<PatternRule> m_rules;
    ::std::vector<PatternBinding> m_bindings;

    static ::Ordering rule_is_before(const PatternRule& l, const PatternRule& r);

    bool is_before(const PatternRuleset& other) const;
};

struct PatternDump {
    const StaticTraitResolve& resolve;
    const HIR::TypeRef& ty;
    const ::std::vector<PatternRule>& rules;

    PatternDump(const StaticTraitResolve& resolve, const HIR::TypeRef& ty, const ::std::vector<PatternRule>& rules)
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
    bool has_condition = false;

    struct Pattern {
        /// Entrypoint for guard and destructuring
        ::MIR::BasicBlockId entry = 0;
        /// Block jumped to by the guard code when the condition fails
        ::MIR::BasicBlockId cond_false = ~0u;
    };

    std::vector<Pattern> rules;
};

typedef ::std::vector<PatternRuleset> t_arm_rules;

void MIR_LowerHIR_Match_Simple(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNode_Match& node, ::MIR::LValue match_val, t_arm_rules arm_rules, ::std::vector<ArmCode> arm_code, ::MIR::BasicBlockId first_cmp_block);
int MIR_LowerHIR_Match_Simple__GeneratePattern(MirBuilder& builder, const Span& sp, const PatternRule* rules, unsigned int num_rules, const ::HIR::TypeRef& top_ty, const ::MIR::LValue& top_val, unsigned int field_path_ofs, ::MIR::BasicBlockId fail_bb);
void MIR_LowerHIR_Match_Grouped(MirBuilder& builder, MirConverter& conv, const Span& sp, const HIR::TypeRef& match_ty, ::MIR::LValue match_val, t_arm_rules arm_rules, ::std::vector<ArmCode> arms_code, ::MIR::BasicBlockId first_cmp_block);
void MIR_LowerHIR_Match_DecisionTree(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNode_Match& node, ::MIR::LValue match_val, t_arm_rules arm_rules, ::std::vector<ArmCode> arm_code, ::MIR::BasicBlockId first_cmp_block);

/// Helper to construct rules from a passed pattern
struct PatternRulesetBuilder {
    const StaticTraitResolve& m_resolve;
    const ::HIR::SimplePath* m_lang_Box = nullptr;

    // NOTE: Multiple rulesets to handle or-patterns (which multiply the pattern set)
    struct Ruleset {
        bool m_is_impossible;
        ::std::vector<PatternRule> m_rules;
        ::std::vector<PatternBinding> m_bindings;
        // Source-order path through nested or-pattern alternatives.  The
        // matching semantics are depth-first and left-to-right, so this path
        // orders the cartesian product after each expansion.
        ::std::vector<unsigned> m_or_path;

        Ruleset()
            : m_is_impossible(false)
        {
        }

        Ruleset clone() const {
            Ruleset rv;
            rv.m_is_impossible = m_is_impossible;
            for (const auto& e : m_rules) {
                rv.m_rules.push_back(e.clone());
            }
            rv.m_bindings = m_bindings;
            rv.m_or_path = m_or_path;
            return rv;
        }
    };

    std::vector<Ruleset> m_rulesets;
    size_t subset_start, subset_end;

    field_path_t m_field_path;

    PatternRulesetBuilder(const StaticTraitResolve& resolve)
        : m_resolve(resolve)
        , m_rulesets(1)
        , subset_start(0)
        , subset_end(1)
    {
        if (resolve.m_crate.m_lang_items.count("owned_box") > 0) {
            m_lang_Box = &resolve.m_crate.m_lang_items.at("owned_box");
        }
    }

    void append_from_lit(const Span& sp, EncodedLiteralSlice lit, const ::HIR::TypeRef& ty);
    void append_from(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::TypeRef& ty);

private:
    void push_rule(PatternRule r);
    void push_binding(PatternBinding b);
    void push_bindings(std::vector<PatternBinding> b);
    void set_impossible();

    void multiply_rulesets(size_t n, std::function<void(size_t idx)> cb);
};

class RulesetRef {
    ::std::vector<PatternRuleset>* m_rules_vec = nullptr;
    RulesetRef* m_parent = nullptr;
    size_t m_parent_ofs = 0; // If len == 0, this is the innner index, else it's the base
    size_t m_parent_len = 0;

public:
    RulesetRef(::std::vector<PatternRuleset>& rules)
        : m_rules_vec(&rules)
    {
    }

    RulesetRef(RulesetRef& parent, size_t start, size_t n)
        : m_parent(&parent)
        , m_parent_ofs(start)
        , m_parent_len(n)
    {
    }

    RulesetRef(RulesetRef& parent, size_t idx)
        : m_parent(&parent)
        , m_parent_ofs(idx)
    {
    }

    size_t size() const {
        if (m_rules_vec) {
            return m_rules_vec->size();
        } else if (m_parent_len) {
            return m_parent_len;
        } else {
            return m_parent->size();
        }
    }

    RulesetRef slice(size_t s, size_t n) {
        return RulesetRef(*this, s, n);
    }

    const ::std::vector<PatternRule>& operator[](size_t i) const {
        if (m_rules_vec) {
            return (*m_rules_vec)[i].m_rules;
        } else if (m_parent_len) {
            return (*m_parent)[m_parent_ofs + i];
        } else {
            // Fun part - Indexes into inner patterns
            const auto& parent_rule = (*m_parent)[i][m_parent_ofs];
            if (const auto* re = parent_rule.opt_Variant()) {
                return re->sub_rules;
            } else {
                throw "TODO";
            }
        }
    }

    void swap(size_t a, size_t b) {
        TRACE_FUNCTION_F(a << ", " << b);
        if (m_rules_vec) {
            ::std::swap((*m_rules_vec)[a], (*m_rules_vec)[b]);
        } else {
            assert(m_parent);
            if (m_parent_len) {
                m_parent->swap(m_parent_ofs + a, m_parent_ofs + b);
            } else {
                m_parent->swap(a, b);
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
void MIR_LowerHIR_Let(MirBuilder& builder, MirConverter& conv, const Span& sp, const ::HIR::Pattern& pat, ::MIR::LValue val, const ::HIR::ExprNode* else_node) {
    TRACE_FUNCTION;

    HIR::TypeRef outer_ty;
    builder.with_val_type(sp, val, [&](const HIR::TypeRef& ty) {
        outer_ty = ty;
    });

    auto success_node = builder.new_bb_unlinked();
    auto first_cmp_block = builder.pause_cur_block();

    // - Convert HIR pattern into ruleset
    std::vector<PatternRuleset> arm_rules;
    std::vector<ArmCode> arm_code;

    auto pat_scope = builder.new_scope_split(sp);

    auto pat_builder = PatternRulesetBuilder{builder.resolve()};
    pat_builder.append_from(sp, pat, outer_ty);
    for (auto& sr : pat_builder.m_rulesets) {
        auto pat_idx = static_cast<unsigned>(&sr - &pat_builder.m_rulesets.front());
        if (sr.m_is_impossible) {
            DEBUG("LET PAT #" << pat_idx << " " << pat << " ==> IMPOSSIBLE [" << sr.m_rules << "]");
        } else {
            DEBUG("LET PAT #" << pat_idx << " " << pat << " ==> [" << sr.m_rules << "]");
            arm_rules.push_back(PatternRuleset{pat_idx, 0, mv$(sr.m_rules), mv$(sr.m_bindings)});

            auto pat_node = builder.new_bb_unlinked();
            builder.set_cur_block(pat_node);
            conv.destructure_from_list(sp, outer_ty, val.clone(), arm_rules.back().m_bindings);
            builder.end_split_arm(sp, pat_scope, /*reachable=*/true);
            builder.end_block(MIR::Terminator::make_Goto(success_node));

            ArmCode::Pattern ap;
            ap.entry = pat_node;
            ArmCode ac;
            ac.rules.push_back(ap);
            arm_code.push_back(ac);
        }
    }
    builder.terminate_scope(sp, mv$(pat_scope));
    if (else_node) {
        // Emit a check (similar to match)
        // NOTE: This is handled by "HIR Lower" currently, seems to work well
        TODO(sp, "Handle let-else");
    }

    MIR_LowerHIR_Match_Grouped(builder, conv, sp, outer_ty, mv$(val), mv$(arm_rules), mv$(arm_code), first_cmp_block);

    builder.set_cur_block(success_node);
}

// Handles lowering non-trivial matches to MIR
// - Non-trivial means that there's more than one pattern
// - Trivial matches are handled using `MIR_LowerHIR_Let`
void MIR_LowerHIR_Match(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNode_Match& node, ::MIR::LValue match_val) {
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
    bool fall_back_on_simple = false;

    const auto& match_ty = node.m_value->m_res_type;
    auto result_val = builder.new_temporary(node.m_res_type);
    auto next_block = builder.new_bb_unlinked();

    /// Top level scope for the match
    auto match_scope = builder.new_scope_loop(node.span());

    // 1. Stop the current block so we can generate code before generating the pattern matching code
    auto first_cmp_block = builder.pause_cur_block();

    /// Entries for each arm, containing the code to run for each
    ::std::vector<ArmCode> arm_code;
    /// Final list of rules (flattened patterns), for all patterns
    t_arm_rules arm_rules;

    // For each arm, generate the contents of the logical `if pattern_matches { if guard { break body; } }`
    for (unsigned int arm_idx = 0; arm_idx < node.m_arms.size(); arm_idx++) {
        TRACE_FUNCTION_FR("ARM " << arm_idx, "ARM " << arm_idx);
        /*const*/ auto& arm = node.m_arms[arm_idx];
        const Span& sp = arm.m_code->span();

        // ---
        // Convert all patterns on this arm into flattened "rules"
        // ---
        auto first_arm_rule_idx = arm_rules.size();
        for (unsigned int pat_idx = 0; pat_idx < arm.m_patterns.size(); pat_idx++) {
            const auto& pat = arm.m_patterns[pat_idx];

            auto pat_builder = PatternRulesetBuilder{builder.resolve()};
            pat_builder.append_from(node.span(), pat, match_ty);
            size_t first_rule = arm_rules.size();
            for (auto& sr : pat_builder.m_rulesets) {
                size_t i = &sr - &pat_builder.m_rulesets.front();
                if (sr.m_is_impossible) {
                    DEBUG("ARM PAT (" << arm_idx << "," << pat_idx << " #" << i << ") " << pat << " ==> IMPOSSIBLE [" << sr.m_rules << "]");
                } else {
                    DEBUG("ARM PAT (" << arm_idx << "," << pat_idx << " #" << i << ") " << pat << " ==> [" << sr.m_rules << "]");
                    // Sort the binding lists, so we can check that the lists are compatible
                    ::std::sort(sr.m_bindings.begin(), sr.m_bindings.end(), [](const PatternBinding& a, const PatternBinding& b) {
                        return a.binding->m_slot < b.binding->m_slot;
                    });
                    // Ensure that all patterns binding to the same set of variables (only check the variables)
                    if (first_rule < arm_rules.size()) {
                        const auto& fr = arm_rules[first_rule];
                        ASSERT_BUG(sp, fr.m_bindings.size() == sr.m_bindings.size(), "Disagreement in bindings between pattern - {" << arm_rules[first_rule].m_bindings << "} vs {" << sr.m_bindings << "}");
                        for (size_t j = 0; j < fr.m_bindings.size(); j++) {
                            ASSERT_BUG(sp, fr.m_bindings[j].binding->m_slot == sr.m_bindings[j].binding->m_slot, "Disagreement in bindings between pattern - {" << arm_rules[first_rule].m_bindings << "} vs {" << sr.m_bindings << "}");
                        }
                    }
                    arm_rules.push_back(PatternRuleset{arm_idx, static_cast<unsigned>(arm_rules.size() - first_arm_rule_idx), mv$(sr.m_rules), mv$(sr.m_bindings)});
                }
            }
        }

        ArmCode ac;

        /// Block allocated for the body code of this arm (jumped to after bindings are set)
        auto arm_body_block = builder.new_bb_unlinked();

        /// Block for when the first rule matches (contains the guard and binding setup for this rule)
        auto entry_block_pat0 = builder.new_bb_unlinked();
        builder.set_cur_block(entry_block_pat0);

        // Split scope for the `if pattern_matches { }` outer arm,
        auto pat_scope = builder.new_scope_split(node.span());
        builder.end_split_arm(sp, pat_scope, /*reachable=*/true); // Inject the `else` case first, this should not push any statements

        // Generate code for this arm (guard, destructuring, and body)
        {
            // Scopes present for the body (generated during guard processing)
            // - Temporary/variable scopes, and split scopes
            struct MatchScope {
                ScopeHandle handle;
                bool is_split;
            };

            std::vector<MatchScope> scopes;

            const auto& bindings0 = arm_rules[first_arm_rule_idx].m_bindings;
            // Create aliases for every binding that only allows shared/immutable access (for use in the guard)
            auto aliases = builder.save_aliases();
            std::vector<unsigned> binding_temps;
            std::vector<unsigned> binding_temps_alt(bindings0.size(), ~0u);
            for (const auto& b : bindings0) {
                HIR::TypeRef final_ty = conv.get_binding_type(sp, b.binding->m_slot);
                const Span& sp = arm.m_code->span();
                auto val = conv.get_value_for_binding_path(sp, match_ty, match_val, b);
                DEBUG("Set alias for: " << *b.binding << " := " << val);
                if (b.binding->m_type != ::HIR::PatternBinding::Type::Move) {
                    const auto& borrow = final_ty->as_Borrow();
                    final_ty = builder.resolve().m_crate.m_types.borrow(::HIR::BorrowType::Shared, borrow.inner, borrow.lifetime);
                    // Not a move binding, still need to borrow but no deref
                    // - Or, make another temporary for the borrow (no scope needed)
                    auto tmp2 = builder.new_temporary(final_ty);
                    binding_temps_alt[binding_temps.size()] = tmp2.as_Local();
                    builder.push_stmt_assign(sp, tmp2.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                    val = std::move(tmp2);
                }
                // Allocate a temporary to hold a borrow of that type
                auto tmp = builder.new_temporary(builder.resolve().m_crate.m_types.borrow(::HIR::BorrowType::Shared, final_ty));
                // - Store the temporary index so later copies can write to it
                binding_temps.push_back(tmp.as_Local());
                // Assign the temporary with a borrow of the other slot
                builder.push_stmt_assign(sp, tmp.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                // And set an alias to point to `*temp`
                builder.add_variable_alias(sp, b.binding->m_slot, ::HIR::PatternBinding::Type::Move, ::MIR::LValue::new_Deref(std::move(tmp)));
            }

            // Require that either there's no guards, or that there's only one rule
            // - Otherwise, we can't (currently) prevent use-after-free
            // This is expected to fail at some point, but more testing needed elsewhere
            bool should_freeze = (!arm.m_guards.empty() && first_arm_rule_idx + 1 < arm_rules.size());
            scopes.push_back({builder.new_scope_freeze(sp), false});
            if (!should_freeze) {
                builder.unfreeze_scope(sp, scopes.front().handle);
            }

            // Block at the start of the saved guard data
            auto block0 = builder.pause_cur_block();
            builder.set_cur_block(block0);
            // Start saving code (the copyable part of the guard, after the assignment of the binding temporaries)
            auto cs_h = builder.code_save_start();
            MIR::BasicBlockId cond_false_block_pat0 = ~0u;
            bool guard_diverged = false;
            // Emit the condtion using the first set of bindings
            if (!arm.m_guards.empty()) {
                auto _dbe = conv.disable_borrow_extension();
                // Emit the guard code
                TRACE_FUNCTION_FR("CONDITIONAL", "CONDITIONAL");

                // The guards are chanined, and all must match for the arm to be taken
                // I.e. These are ANDs
                for (auto& c : arm.m_guards) {
                    const Span& sp = c.val->span();
                    // Emit the logical `if !guard { } else { ... }`

                    /// Block for when this guard successfully matches
                    auto destructure = builder.new_bb_unlinked();

                    // Make a temp scope and push
                    scopes.push_back({builder.new_scope_temp(c.val->span()), false});
                    conv.visit_node_ptr(c.val);
                    if (!builder.block_active()) {
                        guard_diverged = true;
                        break;
                    }
                    MIR::LValue match_cond_val = builder.get_result_in_lvalue(c.val->span(), c.val->m_res_type);
                    DEBUG("GUARD " << c.pat << " = " << match_cond_val);

                    // If this is not a pattern-match, terminate the temporary scope here
                    if (c.is_if) {
                        auto t = builder.new_temporary(c.val->m_res_type);
                        builder.push_stmt_assign(c.val->span(), t.clone(), std::move(match_cond_val));
                        match_cond_val = std::move(t);
                        builder.terminate_scope(sp, std::move(scopes.back().handle));
                        scopes.pop_back();
                    }

                    // Generate simplified rules from patterns
                    auto pat_builder = PatternRulesetBuilder{builder.resolve()};
                    pat_builder.append_from(node.span(), c.pat, c.val->m_res_type);

                    /// Block for when a pattern fails to match
                    auto local_false = builder.new_bb_unlinked();
                    bool local_false_used = false;
                    // OR'd patterns
                    ::std::vector<std::pair<MIR::BasicBlockId, const PatternRulesetBuilder::Ruleset*>> ends;
                    for (auto& sr : pat_builder.m_rulesets) {
                        if (sr.m_is_impossible) {
                            // The rule is impossible, so don't visit
                        } else {
                            if (local_false_used) {
                                local_false = builder.new_bb_unlinked();
                            }

                            ASSERT_BUG(c.val->span(), builder.block_active(), "Block not active");
                            MIR_LowerHIR_Match_Simple__GeneratePattern(builder, c.val->span(), sr.m_rules.data(), sr.m_rules.size(), c.val->m_res_type, match_cond_val, 0, local_false);
                            ends.push_back(std::make_pair(builder.pause_cur_block(), &sr));
                            builder.set_cur_block(local_false);
                            local_false_used = true;
                        }
                    }
                    if (!local_false_used) {
                        // None of the patterns were possible?
                        TODO(sp, "No possible arms in a `if-let` guard?");
                    }
                    if (cond_false_block_pat0 == ~0u) {
                        cond_false_block_pat0 = builder.new_bb_unlinked();
                    }
                    // Split scope for the body of this logical `if`
                    scopes.push_back({builder.new_scope_split(sp), true});
                    builder.end_split_arm(sp, scopes.back().handle, true);
                    // Currently in `local_false`
                    DEBUG("GUARD: Clean up and jump to `cond_false`");
                    // End the top scope early, which also handles ending all intervening scopes
                    builder.terminate_scope_early(sp, scopes.front().handle);
                    // Indicate an exit point to the split
                    builder.end_split_arm(arm.m_code->span(), pat_scope, /*reachable*/ true, /*early*/ true);
                    builder.end_block(::MIR::Terminator::make_Goto(cond_false_block_pat0));

                    // Introduce a local variable scope for the new bindings
                    scopes.push_back({builder.new_scope_var(c.val->span()), false});
                    for (const auto& b : ends.front().second->m_bindings) {
                        builder.define_variable(b.binding->m_slot);
                    }

                    // Only introduce the new bindings (with `destructure_from_list`) after handling the early-exit case
                    // - This stops the `terminate_scope_early` from dropping too eagerly
                    for (const auto& e : ends) {
                        builder.set_cur_block(e.first);
                        conv.destructure_from_list(arm.m_code->span(), c.val->m_res_type, match_cond_val.clone(), e.second->m_bindings, /*update_states=*/&e == ends.data());
                        builder.end_block(::MIR::Terminator::make_Goto(destructure));
                    }

                    ASSERT_BUG(node.span(), !builder.block_active(), "Block still active?");
                    builder.set_cur_block(destructure);
                }
            }
            if (guard_diverged) {
                if (should_freeze) {
                    builder.unfreeze_scope(sp, scopes.front().handle);
                }
                builder.restore_aliases(std::move(aliases));
                auto guard_code = builder.code_save_end(std::move(cs_h));

                while (!scopes.empty()) {
                    builder.terminate_scope(arm.m_code->span(), std::move(scopes.back().handle), false);
                    scopes.pop_back();
                }
                builder.end_split_arm(arm.m_code->span(), pat_scope, /*reachable=*/false);
                builder.terminate_scope(sp, std::move(pat_scope), false);
                builder.terminate_scope_early(sp, match_scope);

                ac.rules.push_back(ArmCode::Pattern{entry_block_pat0, ~0u});
                for (size_t i = first_arm_rule_idx + 1; i < arm_rules.size(); i++) {
                    struct DivergingGuardMapper: public MirBuilder::CloneMapper {
                        MIR::BasicBlockId block0;

                        DivergingGuardMapper(MIR::BasicBlockId block0)
                            : block0(block0)
                        {
                        }

                        MIR::BasicBlockId update_bb_ref(MIR::BasicBlockId bb_idx) override {
                            if (bb_idx < block0) {
                                return bb_idx;
                            }
                            BUG(Span(), "Diverging guard referenced unsaved block bb" << bb_idx << " after bb" << block0);
                        }
                    } mapper(block0);

                    auto entry_block = builder.new_bb_unlinked();
                    builder.set_cur_block(entry_block);
                    ASSERT_BUG(sp, binding_temps.size() == arm_rules[i].m_bindings.size(), "Mismatched guard bindings");
                    for (size_t j = 0; j < binding_temps.size(); j++) {
                        const auto& b = arm_rules[i].m_bindings[j];
                        auto val = conv.get_value_for_binding_path(sp, match_ty, match_val, b);
                        if (b.binding->m_type != ::HIR::PatternBinding::Type::Move) {
                            MIR::LValue tmp2;
                            if (binding_temps_alt[j] == ~0u) {
                                auto final_ty = conv.get_binding_type(sp, b.binding->m_slot);
                                const auto& borrow = final_ty->as_Borrow();
                                final_ty = builder.resolve().m_crate.m_types.borrow(::HIR::BorrowType::Shared, borrow.inner, borrow.lifetime);
                                tmp2 = builder.new_temporary(final_ty);
                                binding_temps_alt[j] = tmp2.as_Local();
                            } else {
                                tmp2 = ::MIR::LValue::new_Local(binding_temps_alt[j]);
                            }
                            builder.push_stmt_assign(sp, tmp2.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                            val = std::move(tmp2);
                        }
                        builder.push_stmt_assign(sp, ::MIR::LValue::new_Local(binding_temps[j]), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                    }
                    builder.insert_cloned(sp, guard_code, mapper);
                    ASSERT_BUG(sp, !builder.block_active(), "Diverging guard clone remained reachable");
                    ac.rules.push_back(ArmCode::Pattern{entry_block, ~0u});
                }

                ac.has_condition = false;
                fall_back_on_simple = true;
                arm_code.push_back(std::move(ac));
                continue;
            }
            // Release the freezing of outer states
            if (should_freeze) {
                // NOTE: The first scope should be the freeze
                builder.unfreeze_scope(sp, scopes.front().handle);
            }
            // And undo aliases
            builder.restore_aliases(std::move(aliases));
            auto guard_end_block = builder.new_bb_unlinked();
            builder.end_block(::MIR::Terminator::make_Goto(guard_end_block));
            auto guard_code = builder.code_save_end(std::move(cs_h));
            builder.set_cur_block(guard_end_block);
            // Emit actual bindings
            DEBUG("Arm " << arm_idx << " rule " << 0 << ":  Destructure");
            scopes.push_back({builder.new_scope_var(arm.m_code->span()), false});
            conv.define_vars_from(node.span(), arm.m_patterns.front());
            conv.destructure_from_list(arm.m_code->span(), match_ty, match_val.clone(), bindings0);
            builder.end_block(::MIR::Terminator::make_Goto(arm_body_block));
            // Emit body code
            DEBUG("-- Body Code");

            scopes.push_back({builder.new_scope_temp(arm.m_code->span()), false});
            builder.set_cur_block(arm_body_block);

            // Push the MovedOut state up into the split's m_cond_state, so that values moved
            // in the pattern are recognized as moved in following branches.
            //builder.end_split_condition( arm.m_code->span(), match_scope );

            conv.visit_node_ptr(arm.m_code);

            if (builder.block_active()) {
                // - Set result
                auto res = builder.get_result(arm.m_code->span());
                builder.push_stmt_assign(arm.m_code->span(), result_val.clone(), mv$(res));
            } else {
                assert(!builder.has_result());
            }
            // Pop/end scopes
            while (!scopes.empty()) {
                if (scopes.back().is_split) {
                    builder.end_split_arm(arm.m_code->span(), scopes.back().handle, /*reachable*/ builder.block_active());
                }
                builder.terminate_scope(arm.m_code->span(), std::move(scopes.back().handle), builder.block_active());
                scopes.pop_back();
            }
            builder.end_split_arm(arm.m_code->span(), pat_scope, /*reachable*/ builder.block_active());
            builder.terminate_scope(sp, std::move(pat_scope), builder.block_active());
            builder.terminate_scope_early(sp, match_scope);

            // Go to the next block (out of the match) (if the body didn't diverge)
            if (builder.block_active()) {
                builder.end_block(::MIR::Terminator::make_Goto(next_block));
            }

            // The first rule just uses the code generated above
            {
                ArmCode::Pattern acp;
                acp.entry = entry_block_pat0;
                acp.cond_false = cond_false_block_pat0;
                ac.rules.push_back(acp);
            }
            // Subsequent rules clone the guard with different values for the bindings, and (importantly) a different failure exit point
            for (size_t i = first_arm_rule_idx + 1; i < arm_rules.size(); i++) {
                TRACE_FUNCTION_FR("Bindings (AR" << i << ")", "Bindings (AR" << i << ")");

                // Clone guard code, with the two exit blocks updated, and references updated
                struct Mapper: public MirBuilder::CloneMapper {
                    MIR::BasicBlockId block0;
                    MIR::BasicBlockId cond_false;
                    MIR::BasicBlockId cond_true;
                    MIR::BasicBlockId new_cond_false;
                    MIR::BasicBlockId new_cond_true;

                    Mapper(MirBuilder& builder, MIR::BasicBlockId block0, MIR::BasicBlockId cond_false, MIR::BasicBlockId cond_true)
                        : block0(block0)
                        , cond_false(cond_false)
                        , cond_true(cond_true)
                        , new_cond_false(builder.new_bb_unlinked())
                        , new_cond_true(builder.new_bb_unlinked())
                    {
                        DEBUG("new_cond_false=" << new_cond_false << ", new_cond_true=" << new_cond_true);
                    }

                    MIR::BasicBlockId update_bb_ref(MIR::BasicBlockId bb_idx) {
                        // Any block defined before the save just propagates through
                        // E.g. if the guard contains a `break`
                        if (bb_idx < block0) {
                            return bb_idx;
                        }
                        if (bb_idx == cond_false) {
                            return new_cond_false;
                        }
                        if (bb_idx == cond_true) {
                            return new_cond_true;
                        }
                        BUG(Span(),
                            "update_bb_ref: Unknown BB " << bb_idx << " "
                                                         << ": block0=" << block0 << ", cond_false=" << cond_false << ", cond_true=" << cond_true);
                    }
                } mapper(builder, block0, cond_false_block_pat0, guard_end_block);

                auto entry_block = builder.new_bb_unlinked();
                builder.set_cur_block(entry_block);
                // Set the binding temporaries with the correct borrows
                assert(binding_temps.size() == arm_rules[i].m_bindings.size());
                for (size_t j = 0; j < binding_temps.size(); j++) {
                    const auto& b = arm_rules[i].m_bindings[j];
                    auto val = conv.get_value_for_binding_path(sp, match_ty, match_val, b);
                    DEBUG("Set alias for: " << *b.binding << " := " << val);
                    if (b.binding->m_type != ::HIR::PatternBinding::Type::Move) {
                        MIR::LValue tmp2;
                        if (binding_temps_alt[j] == ~0u) {
                            // Not a move binding, still need to borrow but no deref
                            // - Or, make another temporary for the borrow (no scope needed)
                            auto final_ty = conv.get_binding_type(sp, b.binding->m_slot);
                            const auto& borrow = final_ty->as_Borrow();
                            final_ty = builder.resolve().m_crate.m_types.borrow(::HIR::BorrowType::Shared, borrow.inner, borrow.lifetime);
                            tmp2 = builder.new_temporary(final_ty);
                            binding_temps_alt[j] = tmp2.as_Local();
                        } else {
                            tmp2 = ::MIR::LValue::new_Local(binding_temps_alt[j]);
                        }
                        builder.push_stmt_assign(sp, tmp2.clone(), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                        val = std::move(tmp2);
                    }
                    builder.push_stmt_assign(sp, ::MIR::LValue::new_Local(binding_temps[j]), ::MIR::RValue::make_Borrow({::HIR::BorrowType::Shared, false, std::move(val)}));
                }
                // Clone the guard contents with updated block references
                builder.insert_cloned(sp, guard_code, mapper);

                // Add the final bindings and jump to the body
                builder.set_cur_block(mapper.new_cond_true);
                DEBUG("Arm " << arm_idx << " rule " << i - first_arm_rule_idx << ":  Destructure");
                conv.destructure_from_list(arm.m_code->span(), match_ty, match_val.clone(), arm_rules[i].m_bindings, /*update_dst_state*/ false);
                builder.end_block(::MIR::Terminator::make_Goto(arm_body_block));

                ArmCode::Pattern acp;
                acp.entry = entry_block;
                acp.cond_false = mapper.new_cond_false;
                ac.rules.push_back(acp);
            }
        }

        // If there is a guard, then flag
        if (!arm.m_guards.empty()) {
            ac.has_condition = true;

            // TODO: What to do with conditionals in the fast model?
            // > Could split the match on each conditional - separating such that if a conditional fails it can fall into the other compatible branches.
            // For now: Disable the complex logic, and fall back to a sequence of checks.
            fall_back_on_simple = true;
        } else {
            ac.has_condition = false;
        }

        arm_code.push_back(std::move(ac));
    }

    // Sort columns of `arm_rules` to maximise effectiveness
    if (arm_rules[0].m_rules.size() > 1) {
        // TODO: Should columns be sorted within equal sub-arms too?
        ::std::vector<unsigned> column_weights(arm_rules[0].m_rules.size());
        for (const auto& arm_rule : arm_rules) {
            ASSERT_BUG(node.span(), column_weights.size() == arm_rule.m_rules.size(), "Arm " << (&arm_rule - &arm_rules.front()) << " size doesn't match first (" << arm_rule.m_rules.size() << " != " << column_weights.size() << ")");
            for (unsigned int i = 0; i < arm_rule.m_rules.size(); i++) {
                if (!arm_rule.m_rules[i].is_Any()) {
                    column_weights.at(i) += 1;
                }
            }
        }

        DEBUG("- Column weights = [" << column_weights << "]");
        // - Sort columns such that the largest (most specific) comes first
        ::std::vector<unsigned> columns_sorted(column_weights.size());
        ::std::iota(columns_sorted.begin(), columns_sorted.end(), 0);
        ::std::sort(columns_sorted.begin(), columns_sorted.end(), [&](auto a, auto b) {
            return column_weights[a] > column_weights[b];
        });
        DEBUG("- Sorted to = [" << columns_sorted << "]");
        for (auto& arm_rule : arm_rules) {
            assert(columns_sorted.size() == arm_rule.m_rules.size());
            ::std::vector<PatternRule> sorted;
            sorted.reserve(columns_sorted.size());
            for (auto idx : columns_sorted) {
                sorted.push_back(mv$(arm_rule.m_rules[idx]));
            }
            arm_rule.m_rules = mv$(sorted);
        }
    }

    for (const auto& arm_rule : arm_rules) {
        DEBUG("> (" << arm_rule.arm_idx << ", " << arm_rule.arm_rule_idx << ") - " << arm_rule.m_rules << (arm_code[arm_rule.arm_idx].has_condition ? " (cond)" : ""));
    }

    // TODO: Remove columns that are all `_`?
    // - Ideally, only accessible structures would be fully destructured like this, making this check redundant

    // Sort rules using the following restrictions:
    // - A rule cannot be reordered across an item that has an overlapping match set
    //  > e.g. nothing can cross _
    //  > equal rules cannot be reordered
    //  > Values cannot cross ranges that contain the value
    //  > This will have to be a bubble sort to ensure that it's correctly stable.
    if (!fall_back_on_simple) {
        sort_rulesets(arm_rules);
        DEBUG("Post-sort");
        for (const auto& arm_rule : arm_rules) {
            DEBUG("> (" << arm_rule.arm_idx << ", " << arm_rule.arm_rule_idx << ") - " << arm_rule.m_rules << (arm_code[arm_rule.arm_idx].has_condition ? " (cond)" : ""));
        }
    }
    // De-duplicate arms (emitting a warning when it happens)
    // - This allows later code to assume that duplicate arms are a codegen bug.
    if (!arm_rules.empty()) {
        for (auto it = arm_rules.begin() + 1; it != arm_rules.end();) {
            // If duplicate rule, (and neither is conditional)
            if ((it - 1)->m_rules == it->m_rules && !arm_code[it->arm_idx].has_condition && !arm_code[(it - 1)->arm_idx].has_condition) {
                WARNING(node.m_arms[it->arm_idx].m_code->span(), W0000, "Duplicate match pattern, unreachable code" << "\n - Pattern : " << PatternDump(builder.resolve(), match_ty, it->m_rules) << "\n - Previous at " << node.m_arms[(it - 1)->arm_idx].m_code->span());
                // Remove
                it = arm_rules.erase(it);
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

    if (fall_back_on_simple) {
        MIR_LowerHIR_Match_Simple(builder, conv, node /*.span(), match_ty*/, mv$(match_val), mv$(arm_rules), mv$(arm_code), first_cmp_block);
    } else {
        MIR_LowerHIR_Match_Grouped(builder, conv, node.span(), match_ty, mv$(match_val), mv$(arm_rules), mv$(arm_code), first_cmp_block);
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
        static std::vector<PatternRule> clone_list(const std::vector<PatternRule>& l) {
            std::vector<PatternRule> rv;
            for (const auto& e : l) {
                rv.push_back(e.clone());
            }
            return rv;
        }

        static PatternRule clone_inner(const PatternRule& t) {
            TU_MATCH_HDRA( (t), {)
            TU_ARMA(Any, te)
                return te;

                TU_ARMA(Variant, te)
                return PatternRule::make_Variant({te.idx, H::clone_list(te.sub_rules)});
                TU_ARMA(Slice, te)
                return PatternRule::make_Slice({te.len, H::clone_list(te.sub_rules)});
                TU_ARMA(SplitSlice, te)
                return PatternRule::make_SplitSlice({te.min_len, te.trailing_len, H::clone_list(te.leading), H::clone_list(te.trailing)});

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

    auto rv = H::clone_inner(*this);
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
    assert(m_rules.size() == other.m_rules.size());
    for (unsigned int i = 0; i < m_rules.size(); i++) {
        const auto& l = m_rules[i];
        const auto& r = other.m_rules[i];
        auto cmp = rule_is_before(l, r);
        if (cmp != ::OrdEqual) {
            return cmp == ::OrdLess;
        }
    }
    return false;
}

void PatternRulesetBuilder::push_rule(PatternRule r) {
    assert(this->subset_start < this->subset_end);
    assert(this->subset_end <= m_rulesets.size());
    for (size_t i = subset_start; i < subset_end; i++) {
        m_rulesets[i].m_rules.push_back(i == subset_end - 1 ? std::move(r) : r.clone());
        m_rulesets[i].m_rules.back().field_path = m_field_path;
    }
}

void PatternRulesetBuilder::push_binding(PatternBinding b) {
    assert(this->subset_start < this->subset_end);
    assert(this->subset_end <= m_rulesets.size());
    for (size_t i = subset_start; i < subset_end; i++) {
        DEBUG(i << " " << b);
        m_rulesets[i].m_bindings.push_back(b);
    }
}

void PatternRulesetBuilder::push_bindings(std::vector<PatternBinding> bindings) {
    assert(this->subset_start < this->subset_end);
    assert(this->subset_end <= m_rulesets.size());
    for (size_t i = subset_start; i < subset_end; i++) {
        auto& l = m_rulesets[i].m_bindings;
        l.insert(l.end(), bindings.begin(), bindings.end());
        DEBUG(i << " [" << bindings << "] = [" << l << "]");
    }
}

void PatternRulesetBuilder::set_impossible() {
    assert(this->subset_start < this->subset_end);
    assert(this->subset_end <= m_rulesets.size());
    for (size_t i = subset_start; i < subset_end; i++) {
        m_rulesets[i].m_is_impossible = true;
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
    assert(this->subset_end <= m_rulesets.size());
    size_t subset_size = this->subset_end - this->subset_start;
    size_t ofs = (n - 1) * subset_size;
    assert(ofs > 0);
    size_t new_subset_end = this->subset_start + n * subset_size;
    size_t n_tail = m_rulesets.size() - this->subset_end;
    DEBUG("subset_size=" << subset_size << ", ofs = " << ofs << ", n_tail=" << n_tail);
    m_rulesets.resize(m_rulesets.size() + (n - 1) * subset_size);
    assert(new_subset_end == m_rulesets.size() - n_tail);
    // Copy the tail out of the way (reverse to avoid chasing itself)
    for (size_t i = m_rulesets.size(); i-- > new_subset_end;) {
        m_rulesets[i] = std::move(m_rulesets[i - ofs]);
    }
    // Copy `n-1` copies of the current subset after itself
    for (size_t j = 1; j < n; j++) {
        for (size_t i = 0; i < subset_size; i++) {
            const auto& src = m_rulesets[this->subset_start + i];
            m_rulesets[this->subset_start + j * subset_size + i] = src.clone();
        }
    }
    for (size_t j = this->subset_start + subset_size; j < new_subset_end; j += subset_size) {
        for (size_t i = 0; i < subset_size; i++) {
            const auto& exp = m_rulesets[this->subset_start + i];
            const auto& a = m_rulesets[j + i];
            ASSERT_BUG(Span(), a.m_rules == exp.m_rules, "BUG: {" << a.m_rules << "} != {" << exp.m_rules << "}");
            ASSERT_BUG(Span(), a.m_bindings == exp.m_bindings, "BUG: {" << a.m_bindings << "} != {" << exp.m_bindings << "}");
        }
    }
    for (size_t i = this->subset_start; i < new_subset_end; i += 1) {
        DEBUG("#" << i << " rules=[" << m_rulesets[i].m_rules << "], bindings=[" << m_rulesets[i].m_bindings << "]");
    }

    // Iterate the new subsets
    size_t saved_start = this->subset_start;
    this->subset_end = this->subset_start;
    for (size_t i = 0; i < n; i++) {
        auto orig_start = this->subset_start;
        this->subset_end += subset_size;
        DEBUG("++ " << i << " " << this->subset_start << " - " << this->subset_end);
        for (size_t j = this->subset_start; j < this->subset_end; j++) {
            m_rulesets[j].m_or_path.push_back(static_cast<unsigned>(i));
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
        m_rulesets.begin() + this->subset_start,
        m_rulesets.begin() + this->subset_end,
        [](const Ruleset& a, const Ruleset& b) {
            return a.m_or_path < b.m_or_path;
        });
    // NOTE: Can't asser that the end is as-expected, as there might be inner subsets created that makes this assumption no longer valid
    //ASSERT_BUG(Span(), this->subset_end == new_subset_end, this->subset_end << " == " << new_subset_end);
    for (size_t i = this->subset_start; i < this->subset_end; i += 1) {
        DEBUG("#" << i << " rules=[" << m_rulesets[i].m_rules << "], bindings=[" << m_rulesets[i].m_bindings << "]");
    }
}

void PatternRulesetBuilder::append_from_lit(const Span& sp, EncodedLiteralSlice lit, const ::HIR::TypeRef& ty) {
    TRACE_FUNCTION_F("lit=" << lit << ", ty=" << ty << ",   m_field_path=[" << m_field_path << "]");

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
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.read_uint(Target_GetPointerBits() / 8), e})));
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
                    this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Int({lit.read_sint(Target_GetPointerBits() / 8), e})));
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
            auto* repr = Target_GetTypeRepr(sp, m_resolve, ty);
            ASSERT_BUG(sp, repr, "Matching with generic constant type not valid - " << ty);
            ASSERT_BUG(sp, e.size() == repr->fields.size(), "Matching tuple with mismatched literal size - " << e.size() << " != " << repr->fields.size());

            m_field_path.push_back(0);
            for (unsigned int i = 0; i < e.size(); i++) {
                this->append_from_lit(sp, lit.slice(repr->fields[i].offset), repr->fields[i].ty);
                m_field_path.back()++;
            }
            m_field_path.pop_back();
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
                    auto* repr = Target_GetTypeRepr(sp, m_resolve, ty);
                    ASSERT_BUG(sp, repr, "Matching with generic constant type not valid - " << ty);

                    m_field_path.push_back(0);
                    for (size_t i = 0; i < repr->fields.size(); i++) {
                        this->append_from_lit(sp, lit.slice(repr->fields[i].offset), repr->fields[i].ty);
                        m_field_path.back()++;
                    }
                    m_field_path.pop_back();
                }
                TU_ARMA(ExternType, pbe) {
                    TODO(sp, "Match extern type");
                }
                TU_ARMA(Union, pbe) {
                    TODO(sp, "Match union");
                }
                TU_ARMA(Enum, pbe) {
                    auto* enm_repr = Target_GetTypeRepr(sp, m_resolve, ty);
                    ASSERT_BUG(sp, enm_repr, "Matching with generic constant type not valid - " << ty);

                    // TODO: Share code with `MIR_Cleanup_LiteralToRValue`
                    auto var_info = enm_repr->get_enum_variant(sp, m_resolve, lit);
                    unsigned var_idx = var_info.first;
                    bool sub_has_tag = var_info.second;

                    PatternRulesetBuilder sub_builder{this->m_resolve};
                    if (enm_repr->fields.size() > 1 || enm_repr->variants.is_None()) {
                        sub_builder.m_field_path = m_field_path;
                        sub_builder.m_field_path.push_back(var_idx);

                        // If the tag is in the sub-type, then ignore.
                        const auto& var_ty = enm_repr->fields[var_idx].ty;
                        auto var_lit = lit.slice(enm_repr->fields[var_idx].offset);
                        // NOTE: The tag is only present if it's an auto-generated struct (i.e. not `()`)
                        if (sub_has_tag && var_ty != m_resolve.m_crate.m_types.unit()) {
                            // This inner type should be a struct
                            DEBUG("Enum variant type w/ tag field: " << var_ty);
                            auto* inner_repr = Target_GetTypeRepr(sp, m_resolve, var_ty);
                            assert(inner_repr->variants.is_None());
                            assert(inner_repr->fields.size() > 0);
                            sub_builder.m_field_path.push_back(0);
                            for (size_t i = 0; i < inner_repr->fields.size() - 1; i++) {
                                sub_builder.append_from_lit(sp, var_lit.slice(inner_repr->fields[i].offset), inner_repr->fields[i].ty);
                                sub_builder.m_field_path.back()++;
                            }
                            sub_builder.m_field_path.pop_back();
                        } else {
                            sub_builder.append_from_lit(sp, var_lit, var_ty);
                        }
                    }

                    ASSERT_BUG(sp, sub_builder.m_rulesets.size() == 1, "Multiple rulesets generated from a literal");
                    this->push_rule(PatternRule::make_Variant({var_idx, mv$(sub_builder.m_rulesets[0].m_rules)}));
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
            ASSERT_BUG(sp, Target_GetSizeOf(sp, m_resolve, e.inner, size), "Matching with generic constant type not valid - " << ty);

            m_field_path.push_back(0);
            size_t ofs = 0;
            for (unsigned int i = 0; i < e.size.as_Known(); i++) {
                this->append_from_lit(sp, lit.slice(ofs, size), e.inner);
                ofs += size;
                m_field_path.back()++;
            }
            m_field_path.pop_back();
        }
        TU_ARMA(Slice, e) {
            TODO(sp, "Match literal Slice");
        }
        TU_ARMA(Borrow, e) {
            m_field_path.push_back(FIELD_DEREF);
            if (e.inner == ::HIR::CoreType::Str) {
                auto ptr_size = Target_GetPointerBits() / 8;
                auto ptr = lit.read_uint(ptr_size).truncate_u64();
                auto len = lit.slice(ptr_size, ptr_size).read_uint(ptr_size).truncate_u64();
                auto* r = lit.get_reloc();
                ASSERT_BUG(sp, r, "Null relocation for string in pattern generation");
                ASSERT_BUG(sp, ptr >= EncodedLiteral::PTR_BASE, "");
                ptr -= EncodedLiteral::PTR_BASE;

                ASSERT_BUG(sp, !r->p, "TODO: Handle &str match constant with non-string relocation - " << *r->p);
                ASSERT_BUG(sp, ptr <= r->bytes.size(), "");
                ASSERT_BUG(sp, len <= r->bytes.size(), "");
                ASSERT_BUG(sp, ptr + len <= r->bytes.size(), "");

                this->push_rule(PatternRule::make_Value(std::string(r->bytes.data() + ptr, r->bytes.data() + ptr + len)));
            } else if (e.inner->is_Slice() && e.inner->as_Slice().inner == ::HIR::CoreType::U8) {
                auto ptr_size = Target_GetPointerBits() / 8;
                auto ptr = lit.read_uint(ptr_size).truncate_u64();
                auto len = lit.slice(ptr_size, ptr_size).read_uint(ptr_size).truncate_u64();
                auto* r = lit.get_reloc();
                ASSERT_BUG(sp, r, "Null relocation for byte-string in pattern generation");
                ASSERT_BUG(sp, ptr >= EncodedLiteral::PTR_BASE, "");
                ptr -= EncodedLiteral::PTR_BASE;

                if (r->p) {
                    ASSERT_BUG(sp, ptr == 0, "TODO: Non-zero offset with reference");
                    MonomorphState val_params(m_resolve.m_crate.m_types);
                    auto v = m_resolve.get_value(sp, *r->p, val_params);
                    ASSERT_BUG(sp, v.is_Static(), "&[u8] match with borrow of non-static (" << *r->p << ") - " << v.tag_str());
                    const HIR::Static& s = *v.as_Static();
                    ASSERT_BUG(sp, s.m_value_generated, "&[u8] match with borrow of non-resolved static (" << *r->p << ")");
                    const EncodedLiteral& val = s.m_value_res;
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
            m_field_path.pop_back();
        }
        TU_ARMA(Pointer, e) {
            // Need to be able to tell downstream to cast to integer before comparison?
            this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({lit.read_uint(Target_GetPointerBits() / 8), HIR::CoreType::Usize})));
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

void PatternRulesetBuilder::append_from(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::TypeRef& top_ty) {
    static ::HIR::Pattern empty_pattern;
    TRACE_FUNCTION_F("pat=" << pat << ", ty=" << top_ty << ",   m_field_path=[" << m_field_path << "]");

    struct H {
        static U128 get_pattern_value_int(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::Pattern::Value& val) {
            TU_MATCH_DEF(::HIR::Pattern::Value, (val), (e), (BUG(sp, "Invalid Value type in " << pat);), (Integer, return e.value;), (Named, assert(e.binding); return EncodedLiteralSlice(e.binding->m_value_res).read_uint();))
            throw "";
        }

        static S128 get_pattern_value_signed(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::Pattern::Value& val) {
            TU_MATCH_DEF(::HIR::Pattern::Value, (val), (e), (BUG(sp, "Invalid signed Value type in " << pat);), (Integer, return S128(e.value);), (Named, assert(e.binding); return EncodedLiteralSlice(e.binding->m_value_res).read_sint();))
            throw "";
        }

        static FloatValue get_pattern_value_float(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::Pattern::Value& val) {
            TU_MATCH_DEF(::HIR::Pattern::Value, (val), (e), (BUG(sp, "Invalid Value type in " << pat);), (Float, return e.value;), (Named, assert(e.binding); return EncodedLiteralSlice(e.binding->m_value_res).read_float();))
            throw "";
        }

        static MIR::Constant get_pattern_value(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::Pattern::Value& val, const ::HIR::CoreType& e) {
            switch (e) {
                case ::HIR::CoreType::F16:
                case ::HIR::CoreType::F32:
                case ::HIR::CoreType::F64:
                case ::HIR::CoreType::F128:
                    // Yes, this is valid.
                    return ::MIR::Constant::make_Float({H::get_pattern_value_float(sp, pat, val), e});
                case ::HIR::CoreType::U8:
                case ::HIR::CoreType::U16:
                case ::HIR::CoreType::U32:
                case ::HIR::CoreType::U64:
                case ::HIR::CoreType::U128:
                case ::HIR::CoreType::Usize:
                    return ::MIR::Constant::make_Uint({H::get_pattern_value_int(sp, pat, val), e});
                case ::HIR::CoreType::I8:
                case ::HIR::CoreType::I16:
                case ::HIR::CoreType::I32:
                case ::HIR::CoreType::I64:
                case ::HIR::CoreType::I128:
                case ::HIR::CoreType::Isize:
                    return ::MIR::Constant::make_Int({H::get_pattern_value_signed(sp, pat, val), e});
                case ::HIR::CoreType::Bool:
                    BUG(sp, "Can't range match on Bool");
                    break;
                case ::HIR::CoreType::Char:
                    // Char is just another name for 'u32'... but with a restricted range
                    return ::MIR::Constant::make_Uint({H::get_pattern_value_int(sp, pat, val), e});
                case ::HIR::CoreType::Str:
                    BUG(sp, "Hit match over `str` - must be `&str`");
                    break;
            }
            throw "";
        }

        static MIR::Constant get_pattern_value_min(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::CoreType& e) {
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

        static MIR::Constant get_pattern_value_max(const Span& sp, const ::HIR::Pattern& pat, const ::HIR::CoreType& e) {
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

    for (const auto& pb : pat.m_bindings) {
        auto path = m_field_path;
        for (size_t i = 0; i < pb.m_implicit_deref_count; i++) {
            path.push_back(FIELD_DEREF);
        }

        this->push_binding(PatternBinding(std::move(path), pb));
    }

    const auto* ty_p = &top_ty;
    for (size_t i = 0; i < pat.m_implicit_deref_count; i++) {
        if (!(*ty_p)->is_Borrow()) {
            BUG(sp, "Deref step " << i << "/" << pat.m_implicit_deref_count << " hit a non-borrow " << *ty_p << " from " << top_ty);
        }
        ty_p = &(*ty_p)->as_Borrow().inner;
        m_field_path.push_back(FIELD_DEREF);
    }
    const auto& ty = *ty_p;

    // TODO: Outer handling for Value::Named patterns
    // - Convert them into either a pattern, or just a variant of this function that operates on ::HIR::Literal
    //  > It does need a way of handling unknown-value constants (e.g. <GenericT as Foo>::CONST)
    //  > Those should lead to a simple match? Or just a custom rule type that indicates that they're checked early
    if (const auto* pe = pat.m_data.opt_Value()) {
        if (const auto* pve = pe->val.opt_Named()) {
            if (pve->binding) {
                // Request consteval
                if (pve->binding->m_value_state == HIR::Constant::ValueState::Unknown) {
                    MonomorphState unused_ms(m_resolve.m_crate.m_types);
                    const HIR::GenericParams* impl_def = nullptr;
                    auto v = m_resolve.get_value(sp, pve->path, unused_ms, false, &impl_def);
                    ConvertHIR_ConstantEvaluate_Constant(m_resolve.m_crate, impl_def, pve->path, const_cast<HIR::Constant&>(*pve->binding));
                }
                ASSERT_BUG(sp, pve->binding->m_value_state == HIR::Constant::ValueState::Known, "Match with an unresolved constant - " << pve->path);
                this->append_from_lit(sp, pve->binding->m_value_res, ty);
                for (size_t i = 0; i < pat.m_implicit_deref_count; i++) {
                    m_field_path.pop_back();
                }
                return;
            } else {
                TODO(sp, "Match with an unbound constant - " << pve->path);
            }
        }
    }

    if (pat.m_data.is_Or()) {
        // Multiply the current pattern (sub)set out, visit with sub-sets
        const auto& e = pat.m_data.as_Or();
        assert(pat.m_implicit_deref_count == 0); // Shouldn't have any, so this code doesn't need to pop them.
        assert(e.size() > 0);
        this->multiply_rulesets(e.size(), [&](size_t i) {
            this->append_from(sp, e[i], top_ty);
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
        TU_MATCH_HDR( (pat.m_data), {)
        default:
            BUG(sp, "Matching primitive with invalid pattern - " << pat);
                TU_ARM(pat.m_data, Any, pe) {
                    this->push_rule(PatternRule::make_Any({}));
                }
                TU_ARM(pat.m_data, Range, pe) {
                    if (!pe.start || !pe.end) {
                        assert(pe.start || pe.end);
                        if (pe.start) {
                            this->push_rule(
                                PatternRule::make_ValueRange({
                                    H::get_pattern_value(sp, pat, *pe.start, e),
                                    H::get_pattern_value_max(sp, pat, e),
                                    true // Inclusive always
                                })
                            );
                        } else {
                            this->push_rule(PatternRule::make_ValueRange({H::get_pattern_value_min(sp, pat, e), H::get_pattern_value(sp, pat, *pe.end, e), pe.is_inclusive}));
                        }
                    } else {
                        this->push_rule(PatternRule::make_ValueRange({H::get_pattern_value(sp, pat, *pe.start, e), H::get_pattern_value(sp, pat, *pe.end, e), pe.is_inclusive}));
                    }
                }
                TU_ARM(pat.m_data, Value, pe) {
                    switch (e) {
                        case ::HIR::CoreType::Bool:
                            // TODO: Support values from `const` too
                            this->push_rule(PatternRule::make_Bool(pe.val.as_Integer().value != 0));
                            break;
                        default:
                            this->push_rule(H::get_pattern_value(sp, pat, pe.val, e));
                            break;
                    }
                }
        }
        }
        TU_ARMA(Tuple, e) {
            m_field_path.push_back(0);
            TU_MATCH_DEF(
                ::HIR::Pattern::Data,
                (pat.m_data),
                (pe),
                (BUG(sp, "Matching tuple with invalid pattern - " << pat);),
                (Any,
                 // TODO: Avoid storing the empty patterns, to save on space/cost
                 for (const auto& sty : e) {
                     this->append_from(sp, empty_pattern, sty);
                     m_field_path.back()++;
                 }),
                (
                    Tuple, assert(e.size() == pe.sub_patterns.size()); for (unsigned int i = 0; i < e.size(); i++) {
                        this->append_from(sp, pe.sub_patterns[i], e[i]);
                        m_field_path.back()++;
                    }
                ),
                (SplitTuple, assert(e.size() >= pe.leading.size() + pe.trailing.size()); unsigned trailing_start = e.size() - pe.trailing.size(); for (unsigned int i = 0; i < e.size(); i++) {
                    if (i < pe.leading.size()) {
                        this->append_from(sp, pe.leading[i], e[i]);
                    } else if (i < trailing_start) {
                        this->append_from(sp, empty_pattern, e[i]);
                    } else {
                        this->append_from(sp, pe.trailing[i - trailing_start], e[i]);
                    }
                    m_field_path.back()++;
                })
            )
            m_field_path.pop_back();
        }
        TU_ARMA(Path, e) {
            struct PH {
                static void push_pattern_tuple(PatternRulesetBuilder& builder, const Span& sp, const ::HIR::Pattern::Data::Data_PathTuple& pe, std::function<const HIR::TypeRef&(const HIR::TypeRef&)> maybe_monomorph) {
                    const auto& sd = ::HIR::pattern_get_tuple(sp, pe.path, pe.binding);
                    assert(sd.size() >= pe.leading.size() + pe.trailing.size());
                    size_t trailing_start = sd.size() - pe.trailing.size();
                    for (unsigned int i = 0; i < sd.size(); i++) {
                        const auto& fld = sd[i];

                        if (i < pe.leading.size()) {
                            builder.append_from(sp, pe.leading[i], maybe_monomorph(fld.ent));
                        } else if (i < trailing_start) {
                            builder.append_from(sp, empty_pattern, maybe_monomorph(fld.ent));
                        } else {
                            builder.append_from(sp, pe.trailing[i - trailing_start], maybe_monomorph(fld.ent));
                        }
                        builder.m_field_path.back()++;
                    }
                }
                static void push_pattern_struct(PatternRulesetBuilder& builder, const Span& sp, const ::HIR::Pattern::Data::Data_PathNamed& pe, std::function<const HIR::TypeRef&(const HIR::TypeRef&)> maybe_monomorph) {
                    const auto& sd = ::HIR::pattern_get_named(sp, pe.path, pe.binding);
                    // NOTE: Iterates in field order (not pattern order) to ensure that patterns are in order between arms
                    for (const auto& fld : sd) {
                        const auto& sty_mono = maybe_monomorph(fld.ty);

                        auto it = ::std::find_if(pe.sub_patterns.begin(), pe.sub_patterns.end(), [&](const auto& x) {
                            return x.first == fld.name;
                        });
                        if (it == pe.sub_patterns.end()) {
                            builder.append_from(sp, empty_pattern, sty_mono);
                        } else {
                            builder.append_from(sp, it->second, sty_mono);
                        }
                        builder.m_field_path.back()++;
                    }
                }
            };
            ::HIR::TypeRef tmp;
            auto maybe_monomorph = [&](const ::HIR::TypeRef& ty) -> const ::HIR::TypeRef& {
                if (monomorphise_type_needed(ty)) {
                    tmp = MonomorphStatePtr(m_resolve.m_crate.m_types, nullptr, &e.path.m_data.as_Generic().m_params, nullptr).monomorph_type(sp, ty);
                    this->m_resolve.expand_associated_types(sp, tmp);
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
                    TU_MATCH_DEF(::HIR::Pattern::Data, (pat.m_data), (pe), (BUG(sp, "Matching opaque type with invalid pattern - " << pat);), (Any, this->push_rule(PatternRule::make_Any({}));))
                }
                TU_ARMA(Struct, pbe) {
                    const auto& str_data = pbe->m_data;

                    if (m_lang_Box && e.path.m_data.as_Generic().m_path == *m_lang_Box) {
                        const auto& inner_ty = e.path.m_data.as_Generic().m_params.m_types.at(0);
                        TU_MATCH_DEF(
                            ::HIR::Pattern::Data,
                            (pat.m_data),
                            (pe),
                            (BUG(sp, "Match not allowed, " << ty << " with " << pat);),
                            (Any,
                             // _ on a box, recurse into the box type.
                             m_field_path.push_back(FIELD_DEREF);
                             this->append_from(sp, empty_pattern, inner_ty);
                             m_field_path.pop_back();),
                            (Box, m_field_path.push_back(FIELD_DEREF); this->append_from(sp, *pe.sub, inner_ty); m_field_path.pop_back();)
                        )
                        break;
                    }
            TU_MATCH_HDRA( (str_data), {)
            TU_ARMA(Unit, sd) {
                TU_MATCH_HDRA( (pat.m_data), {)
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
                            m_field_path.push_back(0);
                TU_MATCH_HDRA( (pat.m_data), {)
                default:
                    BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                                TU_ARMA(Any, pe) {
                                    // - Recurse into type using an empty pattern
                                    for (const auto& fld : sd) {
                                        ASSERT_BUG(sp, m_field_path.back() < FIELD_INDEX_MAX, "Too-large struct field index");
                                        this->append_from(sp, empty_pattern, maybe_monomorph(fld.ent));
                                        m_field_path.back()++;
                                    }
                                }
                                TU_ARMA(PathNamed, pe) {
                                    // Only allow with an empty tuple (assuming that the pattern is also empty)... or if the pattern is a wildcard
                                    if (sd.size() != 0 && !pe.is_wildcard()) {
                                        BUG(sp, "Match not allowed, " << ty << " with " << pat);
                                    }
                                    for (const auto& fld : sd) {
                                        ASSERT_BUG(sp, m_field_path.back() < FIELD_INDEX_MAX, "Too-large struct field index");
                                        this->append_from(sp, empty_pattern, maybe_monomorph(fld.ent));
                                        m_field_path.back()++;
                                    }
                                }
                                TU_ARMA(PathTuple, pe) {
                                    assert(pe.binding.is_Struct());
                                    PH::push_pattern_tuple(*this, sp, pe, maybe_monomorph);
                                }
                }
                m_field_path.pop_back();
                        }
                        TU_ARMA(Named, sd) {
                TU_MATCH_HDRA( (pat.m_data), {)
                default:
                    BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                                TU_ARMA(Any, pe) {
                                    m_field_path.push_back(0);
                                    for (const auto& fld : sd) {
                                        ASSERT_BUG(sp, m_field_path.back() < FIELD_INDEX_MAX, "Too-large struct field index");
                                        this->append_from(sp, empty_pattern, maybe_monomorph(fld.ty));
                                        m_field_path.back()++;
                                    }
                                    m_field_path.pop_back();
                                }
                                TU_ARMA(PathNamed, pe) {
                                    assert(pe.binding.is_Struct());
                                    m_field_path.push_back(0);
                                    PH::push_pattern_struct(*this, sp, pe, maybe_monomorph);
                                    m_field_path.pop_back();
                                }
                }
                        }
            }
                }
                TU_ARMA(Union, pbe) {
            TU_MATCH_HDRA( (pat.m_data), {)
            default:
                TODO(sp, "Match over union - " << ty << " with " << pat);
                        TU_ARMA(Any, pe) {
                            this->push_rule(PatternRule::make_Any({}));
                        }
                        TU_ARMA(PathNamed, pe) {
                            ASSERT_BUG(sp, pe.binding.is_Union() && pe.binding.as_Union() == pbe, "Union pattern binding mismatch");
                            ASSERT_BUG(sp, pe.sub_patterns.size() == 1, "Union pattern must select exactly one field");

                            const auto& field_pattern = pe.sub_patterns.front();
                            auto field_it = ::std::find_if(pbe->m_variants.begin(), pbe->m_variants.end(), [&](const auto& field) {
                                return field.name == field_pattern.first;
                            });
                            ASSERT_BUG(sp, field_it != pbe->m_variants.end(), "Unable to find union field " << field_pattern.first);

                            const auto field_index = static_cast<unsigned>(field_it - pbe->m_variants.begin());
                            ASSERT_BUG(sp, field_index < FIELD_INDEX_MAX, "Too-large union field index");
                            m_field_path.push_back(field_index);
                            this->append_from(sp, field_pattern.second, maybe_monomorph(field_it->ty));
                            m_field_path.pop_back();
                        }
            }
                }
                TU_ARMA(ExternType, pbe) {
            TU_MATCH_HDRA( (pat.m_data), {)
            default:
                BUG(sp, "Match not allowed, " << ty <<  " with " << pat);
                        TU_ARMA(Any, pe) {
                            this->push_rule(PatternRule::make_Any({}));
                        }
            }
                }
                TU_ARMA(Enum, pbe) {
            TU_MATCH_HDRA( (pat.m_data), {)
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

                            PatternRulesetBuilder sub_builder{this->m_resolve};
                            sub_builder.m_field_path = m_field_path;
                            ASSERT_BUG(sp, be.var_idx < FIELD_INDEX_MAX, "Too-large variant index in " << ty);
                            sub_builder.m_field_path.push_back(be.var_idx);
                            sub_builder.m_field_path.push_back(0);

                            PH::push_pattern_tuple(sub_builder, sp, pe, maybe_monomorph);

                            this->multiply_rulesets(sub_builder.m_rulesets.size(), [&](size_t i) {
                                auto& sr = sub_builder.m_rulesets[i];
                                if (sr.m_is_impossible) {
                                    this->set_impossible();
                                }
                                this->push_rule(PatternRule::make_Variant({be.var_idx, mv$(sr.m_rules)}));
                                this->push_bindings(mv$(sr.m_bindings));
                            });
                        }
                        TU_ARMA(PathNamed, pe) {
                            assert(pe.binding.is_Enum());
                            const auto& be = pe.binding.as_Enum();

                            PatternRulesetBuilder sub_builder{this->m_resolve};
                            sub_builder.m_field_path = m_field_path;
                            ASSERT_BUG(sp, be.var_idx < FIELD_INDEX_MAX, "Too-large variant index");
                            sub_builder.m_field_path.push_back(be.var_idx);
                            sub_builder.m_field_path.push_back(0);

                            // Empty variants can be matched with `Var { [..] }` even if they're not struct-like
                            if (be.ptr->is_value()) {
                                assert(pe.sub_patterns.empty());
                            } else if (be.ptr->m_data.as_Data().at(be.var_idx).type == m_resolve.m_crate.m_types.unit()) {
                                assert(pe.sub_patterns.empty());
                            } else if (!be.ptr->m_data.as_Data().at(be.var_idx).is_struct) {
                                assert(pe.sub_patterns.empty());
                                const auto& sd = ::HIR::pattern_get_tuple(sp, pe.path, pe.binding);
                                for (unsigned int i = 0; i < sd.size(); i++) {
                                    const auto& fld = sd[i];
                                    sub_builder.append_from(sp, empty_pattern, maybe_monomorph(fld.ent));
                                    sub_builder.m_field_path.back()++;
                                }
                            } else {
                                PH::push_pattern_struct(sub_builder, sp, pe, maybe_monomorph);
                            }

                            this->multiply_rulesets(sub_builder.m_rulesets.size(), [&](size_t i) {
                                auto& sr = sub_builder.m_rulesets[i];
                                if (sr.m_is_impossible) {
                                    this->set_impossible();
                                }
                                this->push_rule(PatternRule::make_Variant({be.var_idx, mv$(sr.m_rules)}));
                                this->push_bindings(mv$(sr.m_bindings));
                            });
                        }
            }
                }
        }
        }
        TU_ARMA(Generic, e) {
            // Generics don't destructure, so the only valid pattern is `_`
            TU_MATCH_DEF(::HIR::Pattern::Data, (pat.m_data), (pe), (BUG(sp, "Match not allowed, " << ty << " with " << pat);), (Any, this->push_rule(PatternRule::make_Any({}));))
        }
        TU_ARMA(TraitObject, e) {
            if (pat.m_data.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a trait object");
            }
        }
        TU_ARMA(ErasedType, e) {
            if (pat.m_data.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over an erased type");
            }
        }
        TU_ARMA(Array, e) {
            // If the size is unknown, just push a `_` pattern.
            // OR: don't push anything?
            if (!e.size.is_Known()) {
                DEBUG("Matching over unknown-sized array - " << e.size);
                ASSERT_BUG(sp, pat.m_data.is_Any(), "Matching generic-sized array with non `_` pattern - " << pat);
                this->push_rule(PatternRule::make_Any({}));
                break;
            }
            // Sequential match just like tuples.
            m_field_path.push_back(0);
        TU_MATCH_HDRA( (pat.m_data), {)
        default:
            BUG(sp, "Matching array with invalid pattern - " << pat);
                TU_ARMA(Any, pe) {
                    for (unsigned int i = 0; i < e.size.as_Known(); i++) {
                        this->append_from(sp, empty_pattern, e.inner);
                        m_field_path.back()++;
                    }
                }
                TU_ARMA(Slice, pe) {
                    ASSERT_BUG(sp, e.size.as_Known() == pe.sub_patterns.size(), "Pattern size mismatch");
                    for (const auto& v : pe.sub_patterns) {
                        this->append_from(sp, v, e.inner);
                        m_field_path.back()++;
                    }
                }
                TU_ARMA(SplitSlice, pe) {
                    ASSERT_BUG(sp, pe.leading.size() < FIELD_INDEX_MAX, "Too many leading slice rules to fit encodng");
                    const auto array_size = e.size.as_Known();
                    ASSERT_BUG(sp, pe.leading.size() <= array_size, "Too many leading slice rules for array type");
                    ASSERT_BUG(sp, pe.trailing.size() <= array_size - pe.leading.size(), "Too many slice rules for array type");
                    for (const auto& subpat : pe.leading) {
                        this->append_from(sp, subpat, e.inner);
                        m_field_path.back()++;
                    }
                    while (m_field_path.back() < array_size - pe.trailing.size()) {
                        this->append_from(sp, empty_pattern, e.inner);
                        m_field_path.back()++;
                    }
                    for (const auto& subpat : pe.trailing) {
                        this->append_from(sp, subpat, e.inner);
                        m_field_path.back()++;
                    }

                    if (pe.extra_bind.is_valid()) {
                        ASSERT_BUG(sp, pe.extra_bind.m_implicit_deref_count == 0, "");
                        PatternBinding pb(m_field_path, pe.extra_bind);
                        pb.field.pop_back();
                        pb.split_slice = std::make_pair(pe.leading.size(), pe.trailing.size());
                        this->push_binding(mv$(pb));
                    }
                }
        }
        m_field_path.pop_back();
        }
        TU_ARMA(Slice, e) {
        TU_MATCH_HDRA( (pat.m_data), {)
        default:
            BUG(sp, "Matching over [T] with invalid pattern - " << pat);
                TU_ARMA(Any, pe) {
                    this->push_rule(PatternRule::make_Any({}));
                }
                TU_ARMA(Slice, pe) {
                    // Sub-patterns
                    PatternRulesetBuilder sub_builder{this->m_resolve};
                    sub_builder.m_field_path = m_field_path;
                    sub_builder.m_field_path.push_back(0);
                    ASSERT_BUG(sp, pe.sub_patterns.size() < FIELD_INDEX_MAX, "Too many slice rules to fit encodng");
                    for (const auto& subpat : pe.sub_patterns) {
                        sub_builder.append_from(sp, subpat, e.inner);
                        sub_builder.m_field_path.back()++;
                    }

                    // Encodes length check and sub-pattern rules
                    this->multiply_rulesets(sub_builder.m_rulesets.size(), [&](size_t i) {
                        auto& sr = sub_builder.m_rulesets[i];
                        if (sr.m_is_impossible) {
                            this->set_impossible();
                        }
                        this->push_rule(PatternRule::make_Slice({static_cast<unsigned int>(pe.sub_patterns.size()), mv$(sr.m_rules)}));
                        this->push_bindings(mv$(sr.m_bindings));
                    });
                }
                TU_ARMA(SplitSlice, pe) {
                    PatternRulesetBuilder sub_builder{this->m_resolve};
                    sub_builder.m_field_path = m_field_path;
                    ASSERT_BUG(sp, pe.leading.size() < FIELD_INDEX_MAX, "Too many leading slice rules to fit encodng");
                    sub_builder.m_field_path.push_back(0);
                    for (const auto& subpat : pe.leading) {
                        sub_builder.append_from(sp, subpat, e.inner);
                        sub_builder.m_field_path.back()++;
                    }
                    auto leading_rulesets = mv$(sub_builder.m_rulesets);
                    sub_builder.m_rulesets.clear();
                    sub_builder.m_rulesets.resize(1);

                    if (pe.trailing.size()) {
                        // Needs a way of encoding the negative offset in the field path
                        // - For now, just use a very high number (and assert that it's not more than 128)
                        ASSERT_BUG(sp, pe.trailing.size() < FIELD_INDEX_MAX, "Too many trailing slice rules to fit encodng");
                        sub_builder.m_field_path.back() = FIELD_INDEX_MAX + (FIELD_INDEX_MAX - pe.trailing.size());
                        for (const auto& subpat : pe.trailing) {
                            sub_builder.append_from(sp, subpat, e.inner);
                            sub_builder.m_field_path.back()++;
                        }
                    }
                    auto trailing_rulesets = mv$(sub_builder.m_rulesets);

                    if (pe.extra_bind.is_valid()) {
                        ASSERT_BUG(sp, pe.extra_bind.m_implicit_deref_count == 0, "");
                        PatternBinding pb(m_field_path, pe.extra_bind);
                        pb.split_slice = std::make_pair(pe.leading.size(), pe.trailing.size());
                        this->push_binding(mv$(pb));
                    }

                    this->multiply_rulesets(leading_rulesets.size() * trailing_rulesets.size(), [&](size_t i) {
                        size_t i_l = i % leading_rulesets.size();
                        size_t i_t = i / leading_rulesets.size();
                        auto& sr_l = leading_rulesets[i_l];
                        auto& sr_t = trailing_rulesets[i_t];
                        if (sr_l.m_is_impossible || sr_t.m_is_impossible) {
                            this->set_impossible();
                        }

                        this->push_rule(PatternRule::make_SplitSlice({static_cast<unsigned int>(pe.leading.size() + pe.trailing.size()), static_cast<unsigned int>(pe.trailing.size()), mv$(sr_l.m_rules), mv$(sr_t.m_rules)}));
                        this->push_bindings(mv$(sr_l.m_bindings));
                        this->push_bindings(mv$(sr_t.m_bindings));
                    });
                }
        }
        }
        TU_ARMA(Borrow, e) {
            m_field_path.push_back(FIELD_DEREF);
        TU_MATCH_HDR( (pat.m_data), {)
        default:
            BUG(sp, "Matching borrow invalid pattern - " << ty << " with " << pat);
                TU_ARM(pat.m_data, Any, pe) {
                    this->append_from(sp, empty_pattern, e.inner);
                }
                TU_ARM(pat.m_data, Ref, pe) {
                    this->append_from(sp, *pe.sub, e.inner);
                }
                TU_ARM(pat.m_data, Value, pe) {
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
                            m_field_path.push_back(0);
                            for (auto c : s) {
                                this->push_rule(PatternRule::make_Value(::MIR::Constant::make_Uint({U128(static_cast<uint8_t>(c)), ::HIR::CoreType::U8})));
                                m_field_path.back()++;
                            }
                            m_field_path.pop_back();
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
        m_field_path.pop_back();
        }
        TU_ARMA(Pointer, e) {
            if (pat.m_data.is_Any()) {
                this->push_rule(PatternRule::make_Any({}));
            } else {
                ERROR(sp, E0000, "Attempting to match over a pointer");
            }
        }
        TU_ARMA(NamedFunction, e) {
            if (pat.m_data.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a functon pointer");
            }
        }
        TU_ARMA(Function, e) {
            if (pat.m_data.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a functon pointer");
            }
        }
        TU_ARMA(NodeType, e) {
            if (pat.m_data.is_Any()) {
            } else {
                ERROR(sp, E0000, "Attempting to match over a closure/generator/async");
            }
        }
    }
    for(size_t i = 0; i < pat.m_implicit_deref_count; i ++)
    {
        m_field_path.pop_back();
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

    bool found_non_any = false;
    for (size_t i = 0; i < rulesets.size(); i++) {
        assert(idx < rulesets[i].size());
        if (!rulesets[i][idx].is_Any()) {
            found_non_any = true;
        }
    }
    if (found_non_any) {
        TRACE_FUNCTION_F(idx);
        for (size_t i = 0; i < rulesets.size(); i++) {
            DEBUG("- " << i << ": " << rulesets[i]);
        }

        bool action_taken;
        do {
            action_taken = false;
            for (size_t i = 0; i < rulesets.size() - 1; i++) {
                if (rules_overlap(rulesets[i][idx], rulesets[i + 1][idx])) {
                    // Don't move
                } else if (ord_rule_compatible(rulesets[i][idx], rulesets[i + 1][idx]) == OrdGreater) {
                    rulesets.swap(i, i + 1);
                    action_taken = true;
                } else {
                }
            }
        } while (action_taken);
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
    void get_ty_and_val(
        const Span& sp,
        MirBuilder& builder,
        const ::HIR::TypeRef& top_ty,
        const ::MIR::LValue& top_val,
        const field_path_t& field_path,
        unsigned int field_path_ofs,
        /*Out ->*/ ::HIR::TypeRef& out_ty,
        ::MIR::LValue& out_val
    ) {
        const StaticTraitResolve& resolve = builder.resolve();
        ::MIR::LValue lval = top_val.clone();
        ::HIR::TypeRef tmp_ty;
        const ::HIR::TypeRef* cur_ty = &top_ty;

        // TODO: Cache the correspondance of path->type (lval can be inferred)
        ASSERT_BUG(sp, field_path_ofs <= field_path.size(), "Field path offset " << field_path_ofs << " is larger than the path [" << field_path << "]");
        for (unsigned int i = field_path_ofs; i < field_path.size(); i++) {
            unsigned idx = field_path.data[i];
            DEBUG("> " << *cur_ty << " #" << idx);

            TU_MATCH_HDRA( (**cur_ty), {)
            TU_ARMA(Infer, e)   BUG(sp, "Ivar for in match type");
                TU_ARMA(Diverge, e) BUG(sp, "Diverge in match type");
                TU_ARMA(Primitive, e) BUG(sp, "Destructuring a primitive");
                TU_ARMA(Tuple, e) {
                    ASSERT_BUG(sp, idx < e.size(), "Tuple index out of range");
                    lval = ::MIR::LValue::new_Field(mv$(lval), idx);
                    cur_ty = &e[idx];
                }
                TU_ARMA(Path, e) {
                    if (idx == FIELD_DEREF) {
                        auto new_ty = resolve.is_type_owned_box(*cur_ty);
                        ASSERT_BUG(sp, new_ty, "Deref on non-Box - " << *cur_ty);
                        lval = ::MIR::LValue::new_Deref(mv$(lval));
                        cur_ty = new_ty;
                        break;
                    }
                    auto monomorph_to_ptr = [&](const ::HIR::TypeRef& ty) -> const ::HIR::TypeRef* {
                        if (monomorphise_type_needed(ty)) {
                            auto rv = MonomorphStatePtr(resolve.m_crate.m_types, nullptr, &e.path.m_data.as_Generic().m_params, nullptr).monomorph_type(sp, ty);
                            resolve.expand_associated_types(sp, rv);
                            tmp_ty = mv$(rv);
                            return &tmp_ty;
                        } else {
                            return &ty;
                        }
                    };
                TU_MATCH_HDRA( (e.binding), {)
                TU_ARMA(Unbound, pbe) {
                            BUG(sp, "Encounterd unbound path - " << e.path);
                        }
                        TU_ARMA(Opaque, pbe) {
                            BUG(sp, "Destructuring an opaque type - " << *cur_ty);
                        }
                        TU_ARMA(ExternType, pbe) {
                            BUG(sp, "Destructuring an extern type - " << *cur_ty);
                        }
                        TU_ARMA(Struct, pbe) {
                    TU_MATCH_HDRA( (pbe->m_data), { )
                    TU_ARMA(Unit, fields) {
                                    BUG(sp, "Destructuring an unit-like tuple - " << *cur_ty);
                                }
                                TU_ARMA(Tuple, fields) {
                                    ASSERT_BUG(sp, idx < fields.size(), "Tuple struct index (" << idx << ") out of range (" << fields.size() << ") in " << *cur_ty);
                                    const auto& fld = fields[idx];
                                    cur_ty = monomorph_to_ptr(fld.ent);
                                    lval = ::MIR::LValue::new_Field(mv$(lval), idx);
                                }
                                TU_ARMA(Named, fields) {
                                    ASSERT_BUG(sp, idx < fields.size(), "Tuple struct index (" << idx << ") out of range (" << fields.size() << ") in " << *cur_ty);
                                    const auto& fld = fields[idx];
                                    cur_ty = monomorph_to_ptr(fld.ty);
                                    lval = ::MIR::LValue::new_Field(mv$(lval), idx);
                                }
                    }
                        }
                        TU_ARMA(Union, pbe) {
                            ASSERT_BUG(sp, idx < pbe->m_variants.size(), "Union variant index (" << idx << ") out of range (" << pbe->m_variants.size() << ") in " << *cur_ty);
                            const auto& fld = pbe->m_variants[idx];
                            cur_ty = monomorph_to_ptr(fld.ty);
                            lval = ::MIR::LValue::new_Downcast(mv$(lval), idx);
                        }
                        TU_ARMA(Enum, pbe) {
                            ASSERT_BUG(sp, pbe->m_data.is_Data(), "Value enum being destructured - " << *cur_ty);
                            const auto& variants = pbe->m_data.as_Data();
                            ASSERT_BUG(sp, idx < variants.size(), "Variant index (" << idx << ") out of range (" << variants.size() << ") for enum " << *cur_ty);
                            const auto& var = variants[idx];

                            cur_ty = monomorph_to_ptr(var.type);
                            lval = ::MIR::LValue::new_Downcast(mv$(lval), idx);
                        }
                }
                }
                TU_ARMA(Generic, e) {
                    BUG(sp, "Destructuring a generic - " << *cur_ty);
                }
                TU_ARMA(TraitObject, e) {
                    BUG(sp, "Destructuring a trait object - " << *cur_ty);
                }
                TU_ARMA(ErasedType, e) {
                    BUG(sp, "Destructuring an erased type - " << *cur_ty);
                }
                TU_ARMA(Array, e) {
                    cur_ty = &e.inner;
                    if (idx < FIELD_INDEX_MAX) {
                        ASSERT_BUG(sp, idx < e.size.as_Known(), "Index out of range");
                        lval = ::MIR::LValue::new_Field(mv$(lval), idx);
                    } else {
                        idx -= FIELD_INDEX_MAX;
                        idx = FIELD_INDEX_MAX - idx;
                        ASSERT_BUG(sp, idx < e.size.as_Known(), "Index out of range");
                        TODO(sp, "Index " << idx << " from end of array " << lval);
                    }
                }
                TU_ARMA(Slice, e) {
                    cur_ty = &e.inner;
                    if (idx < FIELD_INDEX_MAX) {
                        lval = ::MIR::LValue::new_Field(mv$(lval), idx);
                    } else {
                        idx -= FIELD_INDEX_MAX;
                        idx = FIELD_INDEX_MAX - idx;
                        // 1. Create an LValue containing the size of this slice subtract `idx`
                        auto len_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.get_ptr_to_dst(sp, lval)}));
                        auto sub_val = ::MIR::Param(::MIR::Constant::make_Uint({U128(idx), ::HIR::CoreType::Usize}));
                        auto ofs_val = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_BinOp({mv$(len_lval), ::MIR::eBinOp::SUB, mv$(sub_val)}));
                        // 2. Return _Index with that value
                        lval = ::MIR::LValue::new_Index(mv$(lval), ofs_val.as_Local());
                    }
                }
                TU_ARMA(Borrow, e) {
                    ASSERT_BUG(sp, idx == FIELD_DEREF, "Destructure of borrow doesn't correspond to a deref in the path");
                    //DEBUG(i << " " << *cur_ty << " - " << cur_ty << " " << &tmp_ty);
                    if (cur_ty == &tmp_ty) {
                        tmp_ty = tmp_ty->as_Borrow().inner;
                    } else {
                        cur_ty = &e.inner;
                    }
                    //DEBUG(i << " " << *cur_ty);
                    lval = ::MIR::LValue::new_Deref(mv$(lval));
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

        out_ty = *cur_ty;
        out_val = mv$(lval);
    }
}

// --------------------------------------------------------------------
// Dumb and Simple
// --------------------------------------------------------------------

void MIR_LowerHIR_Match_Simple(MirBuilder& builder, MirConverter& conv, ::HIR::ExprNode_Match& node, ::MIR::LValue match_val, t_arm_rules arm_rules, ::std::vector<ArmCode> arms_code, ::MIR::BasicBlockId first_cmp_block) {
    TRACE_FUNCTION;

    // 1. Generate pattern matches
    builder.set_cur_block(first_cmp_block);
    auto next_arm_bb = builder.new_bb_unlinked();
    size_t prev_arm_idx = !arm_rules.empty() ? arm_rules[0].arm_idx : 0;
    for (const auto& pat_rule : arm_rules) {
        if (pat_rule.arm_idx != prev_arm_idx) {
            DEBUG("New arm (" << prev_arm_idx << " -> " << pat_rule.arm_idx << ")");
            prev_arm_idx = pat_rule.arm_idx;
            builder.end_block(::MIR::Terminator::make_Goto(next_arm_bb));
            builder.set_cur_block(next_arm_bb);
            next_arm_bb = builder.new_bb_unlinked();
        }
        const auto& arm = node.m_arms[pat_rule.arm_idx];
        const auto& rc = arms_code[pat_rule.arm_idx].rules[pat_rule.arm_rule_idx];
        auto next_pattern_bb = builder.new_bb_unlinked();

        // 1. Check
        // - If the ruleset is empty, this is a _ arm over a value
        if (pat_rule.m_rules.size() > 0) {
            MIR_LowerHIR_Match_Simple__GeneratePattern(builder, arm.m_code->span(), pat_rule.m_rules.data(), pat_rule.m_rules.size(), node.m_value->m_res_type, match_val, 0, next_pattern_bb);
        }
        builder.end_block(::MIR::Terminator::make_Goto(rc.entry));

        // - Update the condition's failure target
        if (arms_code[pat_rule.arm_idx].has_condition && (pat_rule.arm_rule_idx == 0 || rc.cond_false != arms_code[pat_rule.arm_idx].rules[0].cond_false)) {
            builder.set_cur_block(rc.cond_false);
            // A guard belongs to this expanded pattern candidate, not to the
            // arm as a whole.  If it fails, another or-pattern candidate from
            // the same arm must still be tested before advancing to the next
            // arm.
            builder.end_block(::MIR::Terminator::make_Goto(next_pattern_bb));
        }

        builder.set_cur_block(next_pattern_bb);
    }
    // - Kill the final pattern block (which is dead code)
    builder.end_block(::MIR::Terminator::make_Diverge({}));
    builder.set_cur_block(next_arm_bb);
    builder.end_block(::MIR::Terminator::make_Diverge({}));
}

int MIR_LowerHIR_Match_Simple__GeneratePattern(MirBuilder& builder, const Span& sp, const PatternRule* rules, unsigned int num_rules, const ::HIR::TypeRef& top_ty, const ::MIR::LValue& top_val, unsigned int field_path_ofs, ::MIR::BasicBlockId fail_bb) {
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

        get_ty_and_val(sp, builder, top_ty, top_val, rule.field_path, field_path_ofs, ity, val);
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
                            builder.end_block(::MIR::Terminator::make_If({val.clone(), succ_bb, fail_bb}));
                        } else {
                            builder.end_block(::MIR::Terminator::make_If({val.clone(), fail_bb, succ_bb}));
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
                                builder.push_stmt_assign(sp, builder.get_if_cond(), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::EQ, mv$(test_val)}));
                                builder.end_block(::MIR::Terminator::make_If({builder.get_if_cond(), succ_bb, fail_bb}));
                                builder.set_cur_block(succ_bb);
                            }
                            TU_ARMA(ValueRange, re) {
                                auto succ_bb = builder.new_bb_unlinked();

                                // IF `val` < `first` : fail_bb
                                if (re.first.as_Uint().v != 0) {
                                    auto test_bb_2 = builder.new_bb_unlinked();
                                    auto test_lt_val = ::MIR::Param(::MIR::Constant::make_Uint({re.first.as_Uint().v, te}));
                                    auto cmp_lt_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(test_lt_val)}));
                                    builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lt_lval), fail_bb, test_bb_2}));

                                    builder.set_cur_block(test_bb_2);
                                }

                                // IF `val` > `last` : fail_bb
                                if (re.last.as_Uint().v == U128::max() && re.is_inclusive) {
                                    builder.end_block(::MIR::Terminator::make_Goto({succ_bb}));
                                } else {
                                    auto test_gt_val = ::MIR::Param(::MIR::Constant::make_Uint({re.last.as_Uint().v, te}));
                                    auto op = re.is_inclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                                    auto cmp_gt_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(test_gt_val)}));
                                    builder.end_block(::MIR::Terminator::make_If({mv$(cmp_gt_lval), fail_bb, succ_bb}));
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
                                auto cmp_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::EQ, mv$(test_val)}));
                                builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lval), succ_bb, fail_bb}));
                                builder.set_cur_block(succ_bb);
                            }
                            TU_ARMA(ValueRange, re) {
                                auto succ_bb = builder.new_bb_unlinked();

                                // IF `val` < `first` : fail_bb
                                if (re.first.as_Int().v != S128::min()) {
                                    auto test_bb_2 = builder.new_bb_unlinked();
                                    auto test_lt_val = ::MIR::Param(::MIR::Constant::make_Int({re.first.as_Int().v, te}));
                                    auto cmp_lt_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(test_lt_val)}));
                                    builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lt_lval), fail_bb, test_bb_2}));
                                    builder.set_cur_block(test_bb_2);
                                }

                                // IF `val` > `last` : fail_bb
                                if (re.last.as_Int().v == S128::max() && re.is_inclusive) {
                                    builder.end_block(::MIR::Terminator::make_Goto({succ_bb}));
                                } else {
                                    auto test_gt_val = ::MIR::Param(::MIR::Constant::make_Int({re.last.as_Int().v, te}));
                                    auto op = re.is_inclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                                    auto cmp_gt_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(test_gt_val)}));
                                    builder.end_block(::MIR::Terminator::make_If({mv$(cmp_gt_lval), fail_bb, succ_bb}));
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
                    auto cmp_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({ ::MIR::Param(val.clone()), ::MIR::eBinOp::EQ, mv$(test_val) }));
                    builder.end_block( ::MIR::Terminator::make_If({ mv$(cmp_lval), succ_bb, fail_bb }) );
                    builder.set_cur_block(succ_bb);
                    ),
                (ValueRange,
                    auto succ_bb = builder.new_bb_unlinked();

                    // IF `val` < `first` : fail_bb
                    if( re.first.as_Uint().v != 0 ) {
                            auto test_bb_2 = builder.new_bb_unlinked();

                            auto test_lt_val = ::MIR::Param(::MIR::Constant::make_Uint({re.first.as_Uint().v, te}));
                            auto cmp_lt_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(test_lt_val)}));
                            builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lt_lval), fail_bb, test_bb_2}));

                            builder.set_cur_block(test_bb_2);
                    }

                    // IF `val` > `last` : fail_bb
                    if(re.last.as_Uint().v >= 0x10FFFF ) {
                            assert(re.is_inclusive);
                            builder.end_block(::MIR::Terminator::make_Goto({succ_bb}));
                    }
                    else {
                            auto test_gt_val = ::MIR::Param(::MIR::Constant::make_Uint({re.last.as_Uint().v, te}));
                            auto op = re.is_inclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                            auto cmp_gt_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(test_gt_val)}));
                            builder.end_block(::MIR::Terminator::make_If({mv$(cmp_gt_lval), fail_bb, succ_bb}));
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
                    auto cmp_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({ val.clone(), ::MIR::eBinOp::EQ, mv$(test_val) }));
                    builder.end_block( ::MIR::Terminator::make_If({ mv$(cmp_lval), succ_bb, fail_bb }) );
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
                            auto cmp_lt_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, mv$(test_lt_val)}));
                            builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lt_lval), fail_bb, test_bb_2}));
                            builder.set_cur_block(test_bb_2);
                    }

                    // IF `val` > `last` : fail_bb
                    if( re.first.as_Float().v == std::numeric_limits<double>::infinity() && re.is_inclusive ) {
                            builder.end_block(::MIR::Terminator::make_Goto({succ_bb}));
                    }
                    else {
                            auto test_gt_val = ::MIR::Param(::MIR::Constant::make_Float({re.last.as_Float().v, te}));
                            auto op = re.is_inclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
                            auto cmp_gt_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, mv$(test_gt_val)}));
                            builder.end_block(::MIR::Terminator::make_If({mv$(cmp_gt_lval), fail_bb, succ_bb}));
                    }

                    builder.set_cur_block(succ_bb);
                    )
                )
                break;
            case ::HIR::CoreType::Str: {
                            ASSERT_BUG(sp, rule.is_Value() && rule.as_Value().is_StaticString(), "Unexpected use of non-value pattern on `str`");
                            const auto& v = rule.as_Value();
                            ASSERT_BUG(sp, val.is_Deref(), "");
                            val.m_wrappers.pop_back();
                            auto str_val = mv$(val);

                            auto succ_bb = builder.new_bb_unlinked();

                            auto test_val = ::MIR::Param(::MIR::Constant(v.as_StaticString()));
                            auto cmp_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(str_val), ::MIR::eBinOp::EQ, mv$(test_val)}));
                            builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lval), succ_bb, fail_bb}));
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
                        const auto& str_data = pbe->m_data;
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
                            auto rv = MonomorphStatePtr(builder.resolve().m_crate.m_types, nullptr, &te.path.m_data.as_Generic().m_params, nullptr).monomorph_type(sp, ty);
                            builder.resolve().expand_associated_types(sp, rv);
                            return rv;
                        };
                        ASSERT_BUG(sp, rule.is_Variant(), "Rule for enum isn't Any or Variant");
                        const auto& re = rule.as_Variant();
                        unsigned int var_idx = re.idx;

                        auto next_bb = builder.new_bb_unlinked();
                        auto var_count = pbe->num_variants();

                        // Generate a switch with only one option different.
                        ::std::vector<::MIR::BasicBlockId> arms(var_count, fail_bb);
                        arms[var_idx] = next_bb;
                        builder.end_block(::MIR::Terminator::make_Switch({val.clone(), mv$(arms)}));

                        builder.set_cur_block(next_bb);

                        if (re.sub_rules.size() > 0) {
                            ASSERT_BUG(sp, pbe->m_data.is_Data(), "Sub-rules present for non-data enum");
                            const auto& variants = pbe->m_data.as_Data();
                            const auto& var_ty = variants.at(re.idx).type;
                            ::HIR::TypeRef tmp;
                            const auto& var_ty_m = (monomorphise_type_needed(var_ty) ? tmp = monomorph(var_ty) : var_ty);

                            // Recurse with the new ruleset
                            MIR_LowerHIR_Match_Simple__GeneratePattern(builder, sp, re.sub_rules.data(), re.sub_rules.size(), var_ty_m, ::MIR::LValue::new_Downcast(val.clone(), var_idx), rule.field_path.size() + 1, fail_bb);
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
                    auto cloned_val = ::MIR::Constant(rule.as_Value().as_Bytes());
                    auto size_val = ::MIR::Constant::make_Uint({U128(rule.as_Value().as_Bytes().size()), ::HIR::CoreType::Usize});

                    auto succ_bb = builder.new_bb_unlinked();

                    ASSERT_BUG(sp, val.is_Deref(), "Slice pattern on non-Deref - " << val);
                    auto inner_val = val.clone_unwrapped();

                    auto slice_rval = ::MIR::RValue::make_MakeDst({mv$(cloned_val), mv$(size_val)});
                    auto test_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.borrow(::HIR::BorrowType::Shared, ty), mv$(slice_rval));
                    auto cmp_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(inner_val), ::MIR::eBinOp::EQ, mv$(test_lval)}));
                    builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lval), succ_bb, fail_bb}));
                    builder.set_cur_block(succ_bb);
                } else if (rule.is_Slice()) {
                    const auto& re = rule.as_Slice();

                    // Compare length
                    auto test_val = ::MIR::Param(::MIR::Constant::make_Uint({U128(re.len), ::HIR::CoreType::Usize}));
                    auto len_val = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.get_ptr_to_dst(sp, val)}));
                    auto cmp_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(len_val), ::MIR::eBinOp::EQ, mv$(test_val)}));

                    auto len_succ_bb = builder.new_bb_unlinked();
                    builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lval), len_succ_bb, fail_bb}));
                    builder.set_cur_block(len_succ_bb);

                    // Recurse checking values
                    MIR_LowerHIR_Match_Simple__GeneratePattern(builder, sp, re.sub_rules.data(), re.sub_rules.size(), top_ty, top_val, field_path_ofs, fail_bb);
                } else if (rule.is_SplitSlice()) {
                    const auto& re = rule.as_SplitSlice();

                    // Compare length
                    auto test_val = ::MIR::Param(::MIR::Constant::make_Uint({U128(re.min_len), ::HIR::CoreType::Usize}));
                    auto len_val = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({builder.get_ptr_to_dst(sp, val)}));
                    auto cmp_lval = builder.lvalue_or_temp(sp, builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(len_val), ::MIR::eBinOp::LT, mv$(test_val)}));

                    auto len_succ_bb = builder.new_bb_unlinked();
                    builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lval), fail_bb, len_succ_bb})); // if len < test : FAIL
                    builder.set_cur_block(len_succ_bb);

                    MIR_LowerHIR_Match_Simple__GeneratePattern(builder, sp, re.leading.data(), re.leading.size(), top_ty, top_val, field_path_ofs, fail_bb);

                    MIR_LowerHIR_Match_Simple__GeneratePattern(builder, sp, re.trailing.data(), re.trailing.size(), top_ty, top_val, field_path_ofs, fail_bb);
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
    ::std::vector<size_t> arm_idxes;

    static ::std::pair<size_t, size_t> decode_arm_idx(size_t v) {
        return ::std::make_pair(v & 0x3FFF, v >> 14);
    }

    static size_t encode_arm_idx(size_t arm_idx, size_t pat_idx) {
        assert(arm_idx <= 0x3FFF);
        assert(pat_idx <= 0x3FFF);
        return arm_idx | (pat_idx << 14);
    }

public:
    t_rules_subset(size_t exp, bool is_arm_indexes)
        : is_arm_indexes(is_arm_indexes)
    {
        rule_sets.reserve(exp);
        arm_idxes.reserve(exp);
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
        size_t arm_rule;
    };

    ArmIdxes arm_idx(size_t n) const {
        assert(is_arm_indexes);
        auto v = decode_arm_idx(arm_idxes.at(n));
        return ArmIdxes{v.first, v.second};
    }

    ::MIR::BasicBlockId bb_idx(size_t n) const {
        assert(!is_arm_indexes);
        return arm_idxes.at(n);
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
            decltype(this->arm_idxes) tmp;
            for (auto i : v) {
                tmp.push_back(arm_idxes[i]);
            }
            ::std::copy(tmp.begin(), tmp.end(), arm_idxes.begin() + start);
        }
    }

    t_rules_subset sub_slice(size_t ofs, size_t n) {
        t_rules_subset rv{n, this->is_arm_indexes};
        rv.rule_sets.reserve(n);
        for (size_t i = 0; i < n; i++) {
            rv.rule_sets.push_back(this->rule_sets[ofs + i]);
            rv.arm_idxes.push_back(this->arm_idxes[ofs + i]);
        }
        return rv;
    }

    void push_arm(const ::std::vector<PatternRule>& x, size_t arm_idx, size_t pat_idx) {
        assert(is_arm_indexes);
        rule_sets.push_back(&x);
        arm_idxes.push_back(encode_arm_idx(arm_idx, pat_idx));
    }

    void push_bb(const ::std::vector<PatternRule>& x, ::MIR::BasicBlockId bb) {
        assert(!is_arm_indexes);
        rule_sets.push_back(&x);
        arm_idxes.push_back(bb);
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const t_rules_subset& x) {
        os << "t_rules_subset{";
        for (size_t i = 0; i < x.rule_sets.size(); i++) {
            if (i != 0) {
                os << ", ";
            }
            os << "[";
            if (x.is_arm_indexes) {
                auto v = decode_arm_idx(x.arm_idxes[i]);
                os << v.first << "," << v.second;
            } else {
                os << "bb" << x.arm_idxes[i];
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
    MirBuilder& m_builder;
    const ::HIR::TypeRef& m_top_ty;
    const ::MIR::LValue& m_top_val;
    const ::std::vector<ArmCode>& m_arms_code;

    size_t m_field_path_ofs;

public:
    MatchGenGrouped(MirBuilder& builder, const Span& sp, const ::HIR::TypeRef& top_ty, const ::MIR::LValue& top_val, const ::std::vector<ArmCode>& arms_code, size_t field_path_ofs)
        : sp(sp)
        , m_builder(builder)
        , m_top_ty(top_ty)
        , m_top_val(top_val)
        , m_arms_code(arms_code)
        , m_field_path_ofs(field_path_ofs)
    {
    }

    void gen_for_slice(t_rules_subset rules, size_t ofs, ::MIR::BasicBlockId default_arm);
    void gen_dispatch(const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& arm_targets, ::MIR::BasicBlockId def_blk);
    void gen_dispatch__primitive(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& arm_targets, ::MIR::BasicBlockId def_blk);
    void gen_dispatch__enum(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& arm_targets, ::MIR::BasicBlockId def_blk);
    void gen_dispatch__slice(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& arm_targets, ::MIR::BasicBlockId def_blk);

    void gen_dispatch_range(const field_path_t& field_path, const ::MIR::Constant& first, const ::MIR::Constant& last, bool is_inclusive, ::MIR::BasicBlockId def_blk);
    void gen_dispatch_splitslice(const field_path_t& field_path, const PatternRule::Data_SplitSlice& e, ::MIR::BasicBlockId def_blk);

    ::MIR::LValue push_compare(::MIR::LValue left, ::MIR::eBinOp op, ::MIR::Param right) {
        return m_builder.lvalue_or_temp(sp, m_builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({mv$(left), op, mv$(right)}));
    }
};

namespace {
    void push_flat_rules(::std::vector<PatternRule>& out_rules, PatternRule rule) {
        TU_MATCH_HDRA( (rule), {)
        TU_ARMA(Variant, e) {
                auto sub_rules = mv$(e.sub_rules);
                out_rules.push_back(mv$(rule));
                for (auto& sr : sub_rules) {
                    push_flat_rules(out_rules, mv$(sr));
                }
            }
            TU_ARMA(Slice, e) {
                auto sub_rules = mv$(e.sub_rules);
                out_rules.push_back(mv$(rule));
                for (auto& sr : sub_rules) {
                    push_flat_rules(out_rules, mv$(sr));
                }
            }
            TU_ARMA(SplitSlice, e) {
                auto leading = mv$(e.leading);
                auto trailing = mv$(e.trailing);
                auto idx = out_rules.size();
                out_rules.push_back(mv$(rule));
                for (auto& sr : leading) {
                    push_flat_rules(out_rules, mv$(sr));
                }
                // Trailing rules are complex as they break the assumption that patterns across the same type share a prefix
                // - So, flatten them into the "flattened" rule
                for (auto& sr : trailing) {
                    push_flat_rules(out_rules[idx].as_SplitSlice().trailing, mv$(sr));
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

    t_arm_rules flatten_rules(t_arm_rules rules) {
        t_arm_rules rv;
        rv.reserve(rules.size());
        for (auto& ruleset : rules) {
            ::std::vector<PatternRule> pattern_rules;
            for (auto& r : ruleset.m_rules) {
                push_flat_rules(pattern_rules, mv$(r));
            }
            rv.push_back(PatternRuleset{ruleset.arm_idx, ruleset.arm_rule_idx, mv$(pattern_rules)});
        }
        return rv;
    }
}

void MIR_LowerHIR_Match_Grouped(MirBuilder& builder, MirConverter& conv, const Span& sp, const HIR::TypeRef& match_ty, ::MIR::LValue match_val, t_arm_rules arm_rules, ::std::vector<ArmCode> arms_code, ::MIR::BasicBlockId first_cmp_block) {
    // TEMPORARY HACK: Grouped fails in complex matches (e.g. librustc_const_math Int::infer)
    //MIR_LowerHIR_Match_Simple( builder, conv, node, mv$(match_val), mv$(arm_rules), mv$(arms_code), first_cmp_block );
    //return;

    TRACE_FUNCTION_F("");

    // Flatten ruleset completely (remove grouping of enum/slice rules)
    arm_rules = flatten_rules(mv$(arm_rules));

    // - Create a "slice" of the passed rules, suitable for passing to the recursive part of the algo
    t_rules_subset rules{arm_rules.size(), /*is_arm_indexes=*/true};
    for (const auto& r : arm_rules) {
        rules.push_arm(r.m_rules, r.arm_idx, r.arm_rule_idx);
    }

    auto inst = MatchGenGrouped{builder, sp, match_ty, match_val, arms_code, 0};

    // NOTE: This block should never be used
    auto default_arm = builder.new_bb_unlinked();

    builder.set_cur_block(first_cmp_block);
    inst.gen_for_slice(mv$(rules), 0, default_arm);

    // Make the default infinite loop.
    // - Preferably, it'd abort.
    builder.set_cur_block(default_arm);
    builder.end_block(::MIR::Terminator::make_Diverge({}));
}

void MatchGenGrouped::gen_for_slice(t_rules_subset arm_rules, size_t ofs, ::MIR::BasicBlockId default_arm) {
    TRACE_FUNCTION_F("arm_rules=" << arm_rules << ", ofs=" << ofs << ", default_arm=" << default_arm);
    ASSERT_BUG(sp, arm_rules.size() > 0, "");

    // Quick hack: Skip any layers entirely made up of PatternRule::Any
    for (;;) {
        bool is_all_any = true;
        for (size_t i = 0; i < arm_rules.size() && is_all_any; i++) {
            if (arm_rules[i].size() <= ofs) {
                is_all_any = false;
            } else if (!arm_rules[i][ofs].is_Any()) {
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
    for (size_t idx = 0; idx < arm_rules.size();) {
        // Completed arms
        while (idx < arm_rules.size() && arm_rules[idx].size() <= ofs) {
            //auto next = idx+1 == arm_rules.size() ? default_arm : m_builder.new_bb_unlinked();
            ASSERT_BUG(sp, arm_rules[idx].size() == ofs, "Offset too large for rule - ofs=" << ofs << ", rules=" << arm_rules[idx]);
            DEBUG(idx << ": Complete");
            // Emit jump to either arm code, or arm condition
            if (arm_rules.is_arm()) {
                auto ai = arm_rules.arm_idx(idx);
                ASSERT_BUG(sp, m_arms_code.size() > 0, "Bottom-level ruleset with no arm code information");
                const auto& ac = m_arms_code[ai.arm];
                ASSERT_BUG(sp, ai.arm_rule < ac.rules.size(), "Arm rule index (" << ai.arm_rule << ") out of bounds (" << ac.rules.size() << ")");

                m_builder.end_block(::MIR::Terminator::make_Goto(ac.rules.at(ai.arm_rule).entry));

                if (ac.has_condition) {
                    TODO(sp, "Handle conditionals in Grouped");
                    // TODO: If the condition fails, this should re-try the match on other rules that could have worked.
                    // - For now, conditionals are disabled.

                    // TODO: What if there's multiple patterns on this condition?
                    // - For now, only the first pattern gets edited.
                    // - Maybe clone the blocks used for the condition?

                } else {
                    ASSERT_BUG(sp, idx + 1 == arm_rules.size(), "Ended arm with other arms present");
                }
            } else {
                auto bb = arm_rules.bb_idx(idx);
                m_builder.end_block(::MIR::Terminator::make_Goto(bb));
                while (idx + 1 < arm_rules.size() && bb == arm_rules.bb_idx(idx) && arm_rules[idx].size() == ofs) {
                    idx++;
                }
                ASSERT_BUG(sp, idx + 1 == arm_rules.size(), "Ended arm (inner) with other arms present");
            }
            idx++;
        }

        // - Value arms
        auto start = idx;
        bool stopped_at_overlap = false;
        for (; idx < arm_rules.size(); idx++) {
            if (arm_rules[idx].size() <= ofs) {
                break;
            }
            if (arm_rules[idx][ofs].is_Any()) {
                break;
            }
            if (arm_rules[idx][ofs].is_SplitSlice()) {
                break;
            }
            // TODO: It would be nice if ValueRange could be combined with Value (if there's no overlap)
            if (arm_rules[idx][ofs].is_ValueRange()) {
                break;
            }

            // The dispatch below sorts selector groups.  Keep an ordering
            // boundary before a selector that overlaps an incompatible
            // earlier selector, otherwise e.g. a byte literal can move past
            // an equal-length slice pattern and change the selected arm.
            for (size_t prev = start; prev < idx; prev++) {
                if (!rule_compatible(arm_rules[prev][ofs], arm_rules[idx][ofs])
                    && rules_overlap(arm_rules[prev][ofs], arm_rules[idx][ofs])) {
                    stopped_at_overlap = true;
                    break;
                }
            }
            if (stopped_at_overlap) {
                break;
            }
        }
        auto first_any = idx;

        // Generate dispatch based on the above list
        // - If there's value ranges they need special handling
        // - Can sort arms within this group (ordering doesn't matter, as long as ranges are handled)
        // - Sort must be stable.

        if (start < first_any) {
            DEBUG(start << "+" << (first_any - start) << ": Values");
            bool has_default = (first_any < arm_rules.size());
            auto next = (has_default ? m_builder.new_bb_unlinked() : default_arm);

            // Sort rules before getting compatible runs
            // TODO: Is this a valid operation?
            arm_rules.sub_sort(ofs, start, first_any - start);

            // Create list of compatible arm slices (runs with the same selector value)
            ::std::vector<t_rules_subset> slices;
            auto cur_test = start;
            for (auto i = start; i < first_any; i++) {
                // Just check if the decision value differs (don't check nested rules)
                if (!rule_compatible(arm_rules[i][ofs], arm_rules[cur_test][ofs])) {
                    slices.push_back(arm_rules.sub_slice(cur_test, i - cur_test));
                    cur_test = i;
                }
            }
            slices.push_back(arm_rules.sub_slice(cur_test, first_any - cur_test));
            DEBUG("- " << slices.size() << " groupings");
            ::std::vector<::MIR::BasicBlockId> arm_blocks;
            arm_blocks.reserve(slices.size());

            auto cur_blk = m_builder.pause_cur_block();
            // > Stable sort list
            ::std::sort(slices.begin(), slices.end(), [&](const auto& a, const auto& b) {
                return a[0][ofs] < b[0][ofs];
            });
            // TODO: Should this do a stable sort of inner patterns too?
            // - A sort of inner patterns such that `_` (and range?) patterns don't change position.

            // > Get type of match, generate dispatch list.
            for (size_t i = 0; i < slices.size(); i++) {
                auto cur_block = m_builder.new_bb_unlinked();
                m_builder.set_cur_block(cur_block);

                for (size_t j = 0; j < slices[i].size(); j++) {
                    if (j > 0) {
                        ASSERT_BUG(sp, slices[i][0][ofs] == slices[i][j][ofs], "Mismatched rules - " << slices[i][0][ofs] << " and " << slices[i][j][ofs]);
                    }
                    arm_blocks.push_back(cur_block);
                }

                this->gen_for_slice(slices[i], ofs + 1, next);
            }

            m_builder.set_cur_block(cur_blk);

            // Generate decision code
            this->gen_dispatch(slices, ofs, arm_blocks, next);

            if (has_default) {
                m_builder.set_cur_block(next);
            }
        }

        if (stopped_at_overlap) {
            continue;
        }

        // Collate matching blocks at `first_any`
        assert(first_any == idx);
        if (first_any < arm_rules.size() && arm_rules[idx].size() > ofs) {
            // Collate all equal rules
            while (idx < arm_rules.size() && arm_rules[idx][ofs] == arm_rules[first_any][ofs]) {
                idx++;
            }
            DEBUG(first_any << "-" << idx << ": Multi-match");

            bool has_next = idx < arm_rules.size();
            auto next = (has_next ? m_builder.new_bb_unlinked() : default_arm);

            const auto& rule = arm_rules[first_any][ofs];
            if (const auto* e = rule.opt_ValueRange()) {
                // Generate branch based on range
                this->gen_dispatch_range(arm_rules[first_any][ofs].field_path, e->first, e->last, e->is_inclusive, next);
            } else if (const auto* e = rule.opt_SplitSlice()) {
                // Generate branch based on slice length being at least required.
                this->gen_dispatch_splitslice(rule.field_path, *e, next);
            } else {
                ASSERT_BUG(sp, rule.is_Any(), "Didn't expect non-Any rule here, got " << rule.tag_str() << " " << rule);
            }

            // Step deeper into these arms
            auto slice = arm_rules.sub_slice(first_any, idx - first_any);
            this->gen_for_slice(mv$(slice), ofs + 1, next);

            if (has_next) {
                m_builder.set_cur_block(next);
            }
        }
    }

    ASSERT_BUG(sp, !m_builder.block_active(), "Block left active after match group");
}

/// <summary>
/// Generate dispatch code for the provided pattern list
/// </summary>
/// <param name="rules">A list of equivalent pattern rules (at the given offset)</param>
/// <param name="ofs">Offset into sub-patterns</param>
/// <param name="arm_targets">Target blocks for each arm in `rules`</param>
/// <param name="def_blk">Default block for if no arm matched</param>
void MatchGenGrouped::gen_dispatch(const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& arm_targets, ::MIR::BasicBlockId def_blk) {
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
        ASSERT_BUG(sp, arm_targets.size() == n, "Arm target count mismatch - " << n << " != " << arm_targets.size());
    }

    ::MIR::LValue val;
    ::HIR::TypeRef ty;
    get_ty_and_val(sp, m_builder, m_top_ty, m_top_val, field_path, m_field_path_ofs, ty, val);
    DEBUG("ty = " << ty << ", val = " << val);

    TU_MATCH_HDRA( (*ty), {)
    TU_ARMA(Infer, te) {
            BUG(sp, "Hit _ in type - " << ty);
        }
        TU_ARMA(Diverge, te) {
            BUG(sp, "Matching over !");
        }
        TU_ARMA(Primitive, te) {
            this->gen_dispatch__primitive(mv$(ty), mv$(val), rules, ofs, arm_targets, def_blk);
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
                    const auto& str_data = pbe->m_data;
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
                    this->gen_dispatch__enum(mv$(ty), mv$(val), rules, ofs, arm_targets, def_blk);
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
            ASSERT_BUG(sp, !val.m_wrappers.empty() && val.m_wrappers.back().is_Deref(), "&[T; N] match on non-Deref lvalue - " << val);
            val.m_wrappers.pop_back();

            ::std::vector<::MIR::BasicBlockId> targets;
            ::std::vector<::std::vector<uint8_t>> values;
            size_t tgt_ofs = 0;
            for (size_t i = 0; i < rules.size(); i++) {
                for (size_t j = 1; j < rules[i].size(); j++) {
                    ASSERT_BUG(sp, arm_targets[tgt_ofs] == arm_targets[tgt_ofs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tag_str());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                targets.push_back(arm_targets[tgt_ofs]);
                values.push_back(re.as_Bytes());

                tgt_ofs += rules[i].size();
            }
            m_builder.end_block(::MIR::Terminator::make_SwitchValue({mv$(val), def_blk, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
        }
        TU_ARMA(Slice, te) {
            this->gen_dispatch__slice(mv$(ty), mv$(val), rules, ofs, arm_targets, def_blk);
        }
        TU_ARMA(Tuple, te) {
            BUG(sp, "Match directly on tuple");
        }
        TU_ARMA(Borrow, te) {
            BUG(sp, "Match directly on borrow");
        }
        TU_ARMA(Pointer, te) {
            auto val_usize = m_builder.new_temporary(m_builder.resolve().m_crate.m_types.primitive(HIR::CoreType::Usize));
            m_builder.push_stmt_assign(sp, val_usize.clone(), ::MIR::RValue::make_Cast({mv$(val), m_builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Usize)}));
            this->gen_dispatch__primitive(m_builder.resolve().m_crate.m_types.primitive(HIR::CoreType::Usize), mv$(val_usize), rules, ofs, arm_targets, def_blk);
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
    void push_if_equal(const Span& sp, MirBuilder& m_builder, ::MIR::LValue val, ::MIR::Param test_val, ::MIR::BasicBlockId bb_true, ::MIR::BasicBlockId bb_false) {
        auto cmp_lval = m_builder.get_rval_in_if_cond(sp, ::MIR::RValue::make_BinOp({mv$(val), ::MIR::eBinOp::EQ, mv$(test_val)}));
        m_builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lval), bb_true, bb_false}));
    }
}

void MatchGenGrouped::gen_dispatch__primitive(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& arm_targets, ::MIR::BasicBlockId def_blk) {
    auto te = ty->as_Primitive();
    switch (te) {
        case ::HIR::CoreType::Bool: {
            ASSERT_BUG(sp, rules.size() <= 2, "More than 2 rules for boolean");
            for (size_t i = 0; i < rules.size(); i++) {
                ASSERT_BUG(sp, rules[i][0][ofs].is_Bool(), "PatternRule for bool isn't _Bool");
            }

            // False sorts before true.
            auto fail_bb = rules.size() == 2 ? arm_targets[0] : (rules[0][0][ofs].as_Bool() ? def_blk : arm_targets[0]);
            auto succ_bb = rules.size() == 2 ? arm_targets[rules[0].size()] : (rules[0][0][ofs].as_Bool() ? arm_targets[0] : def_blk);

            m_builder.end_block(::MIR::Terminator::make_If({mv$(val), succ_bb, fail_bb}));
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
                push_if_equal(sp, m_builder, mv$(val), ::MIR::Param(re.clone()), arm_targets[0], def_blk);
            } else {
                // NOTE: Rules are currently sorted
                // TODO: If there are Constant::Const values in the list, they need to come first! (with equality checks)

                ::std::vector<::std::pair<::MIR::Constant, ::MIR::BasicBlockId>> large_values;
                ::std::vector<uint64_t> values;
                ::std::vector<::MIR::BasicBlockId> targets;
                size_t tgt_ofs = 0;
                for (size_t i = 0; i < rules.size(); i++) {
                    for (size_t j = 1; j < rules[i].size(); j++) {
                        ASSERT_BUG(sp, arm_targets[tgt_ofs] == arm_targets[tgt_ofs + j], "Mismatched target blocks for Value match");
                    }

                    const auto& r = rules[i][0][ofs];
                    ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tag_str());
                    const auto& re = r.as_Value();
                    if (re.is_Const()) {
                        // Inject a `If` chained to a new block
                        auto next_block = m_builder.new_bb_unlinked();
                        push_if_equal(sp, m_builder, val.clone(), ::MIR::Param(re.clone()), arm_targets[tgt_ofs], next_block);
                        m_builder.set_cur_block(next_block);
                    } else if (re.as_Uint().v > U128(UINT64_MAX)) {
                        large_values.push_back(std::make_pair(re.clone(), arm_targets[tgt_ofs]));
                    } else {
                        values.push_back(re.as_Uint().v.truncate_u64());
                        targets.push_back(arm_targets[tgt_ofs]);
                    }

                    tgt_ofs += rules[i].size();
                }
                // If there were any values that don't fit in u64, then emit those as a chain of `if` terminators
                if (!large_values.empty()) {
                    auto tail_block = m_builder.new_bb_unlinked();
                    m_builder.end_block(::MIR::Terminator::make_SwitchValue({val.clone(), tail_block, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
                    m_builder.set_cur_block(tail_block);
                    for (auto& v : large_values) {
                        auto next_block = m_builder.new_bb_unlinked();
                        push_if_equal(sp, m_builder, val.clone(), mv$(v.first), v.second, next_block);
                        m_builder.set_cur_block(next_block);
                    }
                    m_builder.end_block(::MIR::Terminator::make_Goto(def_blk));
                } else {
                    m_builder.end_block(::MIR::Terminator::make_SwitchValue({mv$(val), def_blk, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
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
                push_if_equal(sp, m_builder, mv$(val), ::MIR::Param(re.clone()), arm_targets[0], def_blk);
            } else {
                // NOTE: Rules are currently sorted
                // TODO: If there are Constant::Const values in the list, they need to come first! (with equality checks)

                ::std::vector<int64_t> values;
                ::std::vector<::MIR::BasicBlockId> targets;
                size_t tgt_ofs = 0;
                for (size_t i = 0; i < rules.size(); i++) {
                    for (size_t j = 1; j < rules[i].size(); j++) {
                        ASSERT_BUG(sp, arm_targets[tgt_ofs] == arm_targets[tgt_ofs + j], "Mismatched target blocks for Value match");
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
                    targets.push_back(arm_targets[tgt_ofs]);

                    tgt_ofs += rules[i].size();
                }
                m_builder.end_block(::MIR::Terminator::make_SwitchValue({mv$(val), def_blk, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
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
                    ASSERT_BUG(sp, arm_targets[tgt_ofs] == arm_targets[tgt_ofs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tag_str());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                // IF v < tst : def_blk
                {
                    auto cmp_eq_blk = m_builder.new_bb_unlinked();
                    auto cmp_lval_lt = m_builder.lvalue_or_temp(sp, m_builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::LT, ::MIR::Param(re.clone())}));
                    m_builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lval_lt), def_blk, cmp_eq_blk}));
                    m_builder.set_cur_block(cmp_eq_blk);
                }

                // IF v == tst : target
                {
                    auto next_cmp_blk = m_builder.new_bb_unlinked();
                    auto cmp_lval_eq = m_builder.lvalue_or_temp(sp, m_builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Bool), ::MIR::RValue::make_BinOp({val.clone(), ::MIR::eBinOp::EQ, ::MIR::Param(re.clone())}));
                    m_builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lval_eq), arm_targets[tgt_ofs], next_cmp_blk}));
                    m_builder.set_cur_block(next_cmp_blk);
                }

                tgt_ofs += rules[i].size();
            }
            m_builder.end_block(::MIR::Terminator::make_Goto(def_blk));
        } break;
        case ::HIR::CoreType::Str: {
            // Remove the deref on the &str
            ASSERT_BUG(sp, !val.m_wrappers.empty() && val.m_wrappers.back().is_Deref(), "&str match on non-Deref lvalue - " << val);
            val.m_wrappers.pop_back();

            ::std::vector<::MIR::BasicBlockId> targets;
            ::std::vector<::std::string> values;
            size_t tgt_ofs = 0;
            for (size_t i = 0; i < rules.size(); i++) {
                for (size_t j = 1; j < rules[i].size(); j++) {
                    ASSERT_BUG(sp, arm_targets[tgt_ofs] == arm_targets[tgt_ofs + j], "Mismatched target blocks for Value match");
                }

                const auto& r = rules[i][0][ofs];
                ASSERT_BUG(sp, r.is_Value(), "Matching without _Value pattern - " << r.tag_str());
                const auto& re = r.as_Value();
                if (re.is_Const()) {
                    TODO(sp, "Handle Constant::Const in match");
                }

                targets.push_back(arm_targets[tgt_ofs]);
                values.push_back(re.as_StaticString());

                tgt_ofs += rules[i].size();
            }
            m_builder.end_block(::MIR::Terminator::make_SwitchValue({mv$(val), def_blk, mv$(targets), ::MIR::SwitchValues(mv$(values))}));
        } break;
    }
}

void MatchGenGrouped::gen_dispatch__enum(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& arm_targets, ::MIR::BasicBlockId def_blk) {
    TRACE_FUNCTION;
    auto& te = ty->as_Path();
    const auto& pbe = te.binding.as_Enum();

    auto decison_arm = m_builder.pause_cur_block();

    auto var_count = pbe->num_variants();
    ::std::vector<::MIR::BasicBlockId> arms(var_count, def_blk);
    size_t arm_idx = 0;
    for (size_t i = 0; i < rules.size(); i++) {
        ASSERT_BUG(sp, rules[i][0][ofs].is_Variant(), "Rule for enum isn't Any or Variant - " << rules[i][0][ofs].tag_str());
        const auto& re = rules[i][0][ofs].as_Variant();
        unsigned int var_idx = re.idx;
        DEBUG("Variant " << var_idx);

        ASSERT_BUG(sp, re.sub_rules.size() == 0, "Sub-rules in MatchGenGrouped");

        arms[var_idx] = arm_targets[arm_idx];
        for (size_t j = 0; j < rules[i].size(); j++) {
            assert(arms[var_idx] == arm_targets[arm_idx]);
            arm_idx++;
        }
    }

    m_builder.set_cur_block(decison_arm);
    m_builder.end_block(::MIR::Terminator::make_Switch({mv$(val), mv$(arms)}));
}

void MatchGenGrouped::gen_dispatch__slice(::HIR::TypeRef ty, ::MIR::LValue val, const ::std::vector<t_rules_subset>& rules, size_t ofs, const ::std::vector<::MIR::BasicBlockId>& arm_targets, ::MIR::BasicBlockId def_blk) {
    auto val_len = m_builder.lvalue_or_temp(sp, m_builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({m_builder.get_ptr_to_dst(sp, val)}));

    // TODO: Re-sort the rules list to interleve Constant::Bytes and Slice

    // Just needs to check the lengths, then dispatch.
    size_t tgt_ofs = 0;
    for (size_t i = 0; i < rules.size(); i++) {
        const auto& r = rules[i][0][ofs];
        if (const auto* re = r.opt_Slice()) {
            ASSERT_BUG(sp, re->sub_rules.size() == 0, "Sub-rules in MatchGenGrouped");
            auto val_tst = ::MIR::Constant::make_Uint({U128(re->len), ::HIR::CoreType::Usize});

            for (size_t j = 0; j < rules[i].size(); j++) {
                assert(arm_targets[tgt_ofs] == arm_targets[tgt_ofs + j]);
            }

            // IF v < tst : target
            if (re->len > 0) {
                auto cmp_eq_blk = m_builder.new_bb_unlinked();
                auto cmp_lval_lt = this->push_compare(val_len.clone(), ::MIR::eBinOp::LT, val_tst.clone());
                m_builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lval_lt), def_blk, cmp_eq_blk}));
                m_builder.set_cur_block(cmp_eq_blk);
            }

            // IF v == tst : target
            {
                auto next_cmp_blk = m_builder.new_bb_unlinked();
                auto cmp_lval_eq = this->push_compare(val_len.clone(), ::MIR::eBinOp::EQ, mv$(val_tst));
                m_builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lval_eq), arm_targets[tgt_ofs], next_cmp_blk}));
                m_builder.set_cur_block(next_cmp_blk);
            }
        } else if (const auto* re = r.opt_Value()) {
            ASSERT_BUG(sp, re->is_Bytes(), "Slice with non-Bytes value - " << *re);
            const auto& b = re->as_Bytes();

            auto val_tst_len = ::MIR::Constant::make_Uint({U128(b.size()), ::HIR::CoreType::Usize});

            // IF v == tst : target
            {
                auto next_cmp_blk = m_builder.new_bb_unlinked();

                // TODO: What if `val` isn't a Deref?
                ASSERT_BUG(sp, !val.m_wrappers.empty() && val.m_wrappers.back().is_Deref(), "TODO: Handle non-Deref matches of byte strings - " << val);
                auto& types = m_builder.resolve().m_crate.m_types;
                auto cmp_slice_val = m_builder.lvalue_or_temp(sp, types.borrow(::HIR::BorrowType::Shared, types.slice(types.primitive(::HIR::CoreType::U8))), ::MIR::RValue::make_MakeDst({::MIR::Param(re->clone()), val_tst_len.clone()}));
                auto cmp_lval_eq = this->push_compare(val.clone_unwrapped(), ::MIR::eBinOp::EQ, mv$(cmp_slice_val));
                m_builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lval_eq), arm_targets[tgt_ofs], next_cmp_blk}));

                m_builder.set_cur_block(next_cmp_blk);
            }
        } else {
            BUG(sp, "Matching without _Slice pattern - " << r.tag_str() << " - " << r);
        }

        tgt_ofs += rules[i].size();
    }
    m_builder.end_block(::MIR::Terminator::make_Goto(def_blk));
}

void MatchGenGrouped::gen_dispatch_range(const field_path_t& field_path, const ::MIR::Constant& first, const ::MIR::Constant& last, bool is_inclusive, ::MIR::BasicBlockId def_blk) {
    TRACE_FUNCTION_F("field_path=" << field_path << ", " << first << " .." << (is_inclusive ? "=" : "") << " " << last);
    ::MIR::LValue val;
    ::HIR::TypeRef ty;
    get_ty_and_val(sp, m_builder, m_top_ty, m_top_val, field_path, m_field_path_ofs, ty, val);
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
            auto test_bb_2 = m_builder.new_bb_unlinked();
            // IF `val` < `first` : fail_bb
            auto cmp_lt_lval = m_builder.get_rval_in_if_cond(sp, ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), ::MIR::eBinOp::LT, ::MIR::Param(first.clone())}));
            m_builder.end_block(::MIR::Terminator::make_If({mv$(cmp_lt_lval), def_blk, test_bb_2}));

            m_builder.set_cur_block(test_bb_2);
        }

        if (upper_possible) {
            auto succ_bb = m_builder.new_bb_unlinked();

            // IF `val` > `last` : fail_bb
            auto op = is_inclusive ? ::MIR::eBinOp::GT : ::MIR::eBinOp::GE;
            auto cmp_gt_lval = m_builder.get_rval_in_if_cond(sp, ::MIR::RValue::make_BinOp({::MIR::Param(val.clone()), op, ::MIR::Param(last.clone())}));
            m_builder.end_block(::MIR::Terminator::make_If({mv$(cmp_gt_lval), def_blk, succ_bb}));

            m_builder.set_cur_block(succ_bb);
        }
    } else {
        TODO(sp, "ValueRange on " << ty);
    }
}

void MatchGenGrouped::gen_dispatch_splitslice(const field_path_t& field_path, const PatternRule::Data_SplitSlice& e, ::MIR::BasicBlockId def_blk) {
    TRACE_FUNCTION_F("field_path=" << field_path << ", [" << e.leading << ", .., " << e.trailing << "]");
    ::MIR::LValue val;
    ::HIR::TypeRef ty;
    get_ty_and_val(sp, m_builder, m_top_ty, m_top_val, field_path, m_field_path_ofs, ty, val);
    DEBUG("ty = " << ty << ", val = " << val);

    ASSERT_BUG(sp, e.leading.size() == 0, "Sub-rules in MatchGenGrouped");
    ASSERT_BUG(sp, ty->is_Slice(), "SplitSlice pattern on non-slice - " << ty);

    // Obtain slice length
    auto val_len = m_builder.lvalue_or_temp(sp, m_builder.resolve().m_crate.m_types.primitive(::HIR::CoreType::Usize), ::MIR::RValue::make_DstMeta({m_builder.get_ptr_to_dst(sp, val)}));

    // 1. Check that length is sufficient for the pattern to be used
    // `IF len < min_len : def_blk, next
    {
        auto next = m_builder.new_bb_unlinked();
        auto cmp_val = this->push_compare(val_len.clone(), ::MIR::eBinOp::LT, ::MIR::Constant::make_Uint({U128(e.min_len), ::HIR::CoreType::Usize}));
        m_builder.end_block(::MIR::Terminator::make_If({mv$(cmp_val), def_blk, next}));
        m_builder.set_cur_block(next);
    }

    // 2. Recurse into leading patterns.
    // TODO: This is dead code (leading patterns should have been expanded, and there's an assert above for it)
    if (e.min_len > e.trailing_len) {
        auto next = m_builder.new_bb_unlinked();
        auto inner_set = t_rules_subset{1, /*is_arm_indexes=*/false};
        inner_set.push_bb(e.leading, next);
        auto inst = MatchGenGrouped{m_builder, sp, ty, val, {}, field_path.size()};
        inst.gen_for_slice(inner_set, 0, def_blk);

        m_builder.set_cur_block(next);
    }

    // 3. Recurse into trailing patterns
    if (e.trailing_len != 0) {
        auto next = m_builder.new_bb_unlinked();
        auto inner_set = t_rules_subset{1, /*is_arm_indexes=*/false};
        inner_set.push_bb(e.trailing, next);
        auto inst = MatchGenGrouped{m_builder, sp, ty, val, {}, field_path.size()};
        inst.gen_for_slice(inner_set, 0, def_blk);

        m_builder.set_cur_block(next);
    }
}

#include <algorithm>
#include "mir_from_hir.h"

// --------------------------------------------------------------------
// MirBuilder
// --------------------------------------------------------------------
MirBuilder::MirBuilder(const Span& sp, const StaticTraitResolve& resolve, const ::HIR::TypeRef& ret_ty, const ::HIR::Function::args_t& args, ::MIR::Function& output)
    : m_root_span(sp)
    , m_resolve(resolve)
    , m_ret_ty(ret_ty)
    , m_args(args)
    , m_output(output)
    , m_lang_Box(nullptr)
    , m_block_active(false)
    , m_result_valid(false)
    , m_fcn_scope(*this, 0)
{
    if (resolve.m_crate.m_lang_items.count("owned_box") > 0) {
        m_lang_Box = &resolve.m_crate.m_lang_items.at("owned_box");
    }

    set_cur_block(new_bb_unlinked());
    m_scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({false, {}})});
    m_scope_stack.push_back(0);

    m_scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({true, {}})});
    m_scope_stack.push_back(1);

    m_arg_states.reserve(args.size());
    for (size_t i = 0; i < args.size(); i++) {
        m_arg_states.push_back(VarState::make_Valid({}));
    }
    m_slot_states.resize(output.locals.size());
    m_first_temp_idx = output.locals.size();
    DEBUG("First temporary will be " << m_first_temp_idx);

    m_if_cond_lval = this->new_temporary(m_resolve.m_crate.m_types.primitive(::HIR::CoreType::Bool));

    // Determine which variables can be replaced by arguents
    for (size_t i = 0; i < args.size(); i++) {
        const auto& pat = args[i].first;
        if (pat.m_bindings.size() == 1 && pat.m_bindings[0].m_type == ::HIR::PatternBinding::Type::Move) {
            DEBUG("Argument shortcut: " << pat.m_bindings[0] << " -> a" << i);
            m_var_arg_mappings[pat.m_bindings[0].m_slot] = i;
        }
    }

    m_variable_aliases.resize(output.locals.size());
}

void MirBuilder::final_cleanup() {
    TRACE_FUNCTION_F("");
    const auto& sp = m_root_span;
    if (block_active()) {
        if (m_ret_ty->is_Diverge()) {
            terminate_scope_early(sp, fcn_scope());
            // Validation fails if this is reachable.
            //end_block( ::MIR::Terminator::make_Incomplete({}) );
            end_block(::MIR::Terminator::make_Diverge({}));
        } else {
            if (has_result()) {
                push_stmt_assign(sp, ::MIR::LValue::new_Return(), get_result(sp));
            }

            terminate_scope_early(sp, fcn_scope());

            end_block(::MIR::Terminator::make_Return({}));
        }
    } else {
        terminate_scope(sp, ScopeHandle(*this, 1), /*emit_cleanup=*/false);
        terminate_scope(sp, mv$(m_fcn_scope), /*emit_cleanup=*/false);
    }

    // Rewrite drop flags
    // - Expand recursive lookups
    for (;;) {
        bool added = false;
        for (auto& a : m_drop_flag_aliases) {
            auto& mapped_flags = a.second;
            // Iterate every "destination" flag
            for (size_t i = 0; i < mapped_flags.size(); i++) {
                auto it2 = m_drop_flag_aliases.find(mapped_flags[i]);
                if (it2 != m_drop_flag_aliases.end()) {
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

    for (auto& b : m_output.blocks) {
        for (auto it = b.statements.begin(); it != b.statements.end(); ++it) {
            // NOTE: Only need to worry about SetDropFlag, as the other ways of setting a flag are not generated yet.
            if (auto* p = it->opt_SetDropFlag()) {
                // Take a copy, which will be mutated to create the copies
                auto v = *p;
                auto df_it = m_drop_flag_aliases.find(v.idx);
                if (df_it != m_drop_flag_aliases.end()) {
                    // For each entry in `df_it->second`, add a copy of this SetDropFlag _before_ `it` (so it doesn't get re-visited)
                    for (unsigned other_idx : df_it->second) {
                        v.idx = other_idx;
                        // Ensure that `it` always points to the original
                        it = b.statements.insert(it, ::MIR::Statement(v)) + 1;
                    }
                }
            }
        }
    }
}

const ::HIR::TypeRef* MirBuilder::is_type_owned_box(const ::HIR::TypeRef& ty) const {
    if (m_lang_Box) {
        if (!ty->is_Path()) {
            return nullptr;
        }
        const auto& te = ty->as_Path();

        if (!te.path.m_data.is_Generic()) {
            return nullptr;
        }
        const auto& pe = te.path.m_data.as_Generic();

        if (pe.m_path != *m_lang_Box) {
            return nullptr;
        }
        // TODO: Properly assert the size?
        return &pe.m_params.m_types.at(0);
    } else {
        return nullptr;
    }
}

void MirBuilder::define_variable(unsigned int idx) {
    DEBUG("DEFINE (var) _" << idx << ": " << m_output.locals.at(idx));
    for (auto scope_idx : ::reverse(m_scope_stack)) {
        auto& scope_def = m_scopes.at(scope_idx);
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

::MIR::LValue MirBuilder::new_temporary(const ::HIR::TypeRef& ty) {
    unsigned int rv = m_output.locals.size();
    DEBUG("DEFINE (temp) _" << rv << ": " << ty);

    assert(m_output.locals.size() == m_slot_states.size());
    m_output.locals.push_back(ty);
    m_slot_states.push_back(VarState::make_Invalid(InvalidType::Uninit));
    assert(m_output.locals.size() == m_slot_states.size());

    ScopeDef* top_scope = nullptr;
    for (unsigned int i = m_scope_stack.size(); i--;) {
        auto idx = m_scope_stack[i];
        if (const auto* e = m_scopes.at(idx).data.opt_Owning()) {
            if (e->is_temporary) {
                top_scope = &m_scopes.at(idx);
                break;
            }
        } else if (m_scopes.at(idx).data.is_Loop()) {
            // Newly created temporary within a loop, if there is a saved
            // state this temp needs a drop flag.
            // TODO: ^
        } else if (m_scopes.at(idx).data.is_Split()) {
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
    return ::MIR::LValue::new_Local(rv);
}

::MIR::LValue MirBuilder::lvalue_or_temp(const Span& sp, const ::HIR::TypeRef& ty, ::MIR::RValue val) {
    TU_IFLET(::MIR::RValue, val, Use, e, return mv$(e);)
    else {
        auto temp = new_temporary(ty);
        push_stmt_assign(sp, temp.clone(), mv$(val));
        return temp;
    }
}

::MIR::RValue MirBuilder::get_result(const Span& sp) {
    if (!m_result_valid) {
        BUG(sp, "No value avaliable");
    }
    auto rv = mv$(m_result);
    m_result_valid = false;
    DEBUG(rv);
    return rv;
}

::MIR::LValue MirBuilder::get_result_unwrap_lvalue(const Span& sp) {
    auto rv = get_result(sp);
    TU_IFLET(::MIR::RValue, rv, Use, e, return mv$(e);)
    else {
        BUG(sp, "LValue expected, got RValue");
    }
}

::MIR::LValue MirBuilder::get_result_in_lvalue(const Span& sp, const ::HIR::TypeRef& ty, bool allow_missing_value /*=false*/) {
    if (allow_missing_value && !block_active()) {
        return new_temporary(ty);
    }
    auto rv = get_result(sp);
    TU_IFLET(::MIR::RValue, rv, Use, e, return mv$(e);)
    else {
        auto temp = new_temporary(ty);
        push_stmt_assign(sp, ::MIR::LValue(temp.clone()), mv$(rv));
        return temp;
    }
}

::MIR::Param MirBuilder::get_result_in_param(const Span& sp, const ::HIR::TypeRef& ty, bool allow_missing_value) {
    if (allow_missing_value && !block_active()) {
        return new_temporary(ty);
    }

    auto rv = get_result(sp);
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
    if (m_result_valid) {
        BUG(sp, "Pushing a result over an existing result");
    }
    m_result = mv$(val);
    m_result_valid = true;
    DEBUG(m_result);
}

void MirBuilder::push_stmt_assign(const Span& sp, ::MIR::LValue dst, ::MIR::RValue val, bool update_dest_state /*=true*/) {
    DEBUG(dst << " = " << val);
    ASSERT_BUG(sp, m_block_active, "Pushing statement with no active block");

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
    ASSERT_BUG(sp, m_block_active, "Pushing statement with no active block");

    if (lvalue_is_copy(sp, val)) {
        // Don't emit a drop for Copy values
        return;
    }

    this->push_stmt(sp, ::MIR::Statement::make_Drop({::MIR::eDropKind::DEEP, mv$(val), flag}));
}

void MirBuilder::push_stmt_drop_shallow(const Span& sp, ::MIR::LValue val, unsigned int flag /*=~0u*/) {
    ASSERT_BUG(sp, m_block_active, "Pushing statement with no active block");

    // TODO: Ensure that the type is a Box?

    this->push_stmt(sp, ::MIR::Statement::make_Drop({::MIR::eDropKind::SHALLOW, mv$(val), flag}));
}

void MirBuilder::push_stmt_asm(const Span& sp, ::MIR::Statement::Data_Asm data) {
    ASSERT_BUG(sp, m_block_active, "Pushing statement with no active block");

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
    this->push_stmt(sp, ::MIR::Statement::make_SetDropFlag({idx, this->get_drop_flag_default(sp, idx), ~0u}));
}

void MirBuilder::push_stmt(const Span& sp, ::MIR::Statement stmt) {
    ASSERT_BUG(sp, m_block_active, "Pushing statement with no active block");
    auto& blk = m_output.blocks.at(m_current_block);
    DEBUG("BB" << m_current_block << "/" << blk.statements.size() << " = " << stmt);
    blk.statements.push_back(mv$(stmt));
}

void MirBuilder::mark_value_assigned(const Span& sp, const ::MIR::LValue& dst) {
    if (dst.m_root.is_Return()) {
        ASSERT_BUG(sp, dst.m_wrappers.empty(), "Assignment to a component of the return value should be impossible.");
        return;
    }
    VarState* state_p = get_val_state_mut_p(sp, dst, /*expect_valid=*/true);

    if (state_p) {
        TU_IFLET(VarState, (*state_p), Invalid, se, ASSERT_BUG(sp, se != InvalidType::Descoped, "Assining of descoped variable - " << dst);)
        drop_value_from_state(sp, *state_p, dst.clone());
        auto new_state = VarState::make_Valid({});
        DEBUG("State " << dst << " " << *state_p << " => " << new_state);
        *state_p = std::move(new_state);
    } else {
        // Assigning into non-tracked locations still causes a drop
        drop_value_from_state(sp, VarState::make_Valid({}), dst.clone());
    }
}

void MirBuilder::raise_temporaries(const Span& sp, const ::MIR::LValue& val, const ScopeHandle& scope, bool to_above /*=false*/) {
    TRACE_FUNCTION_F(val);
    for (const auto& w : val.m_wrappers) {
        if (w.is_Index()) {
            // Raise index temporary
            raise_temporaries(sp, ::MIR::LValue::new_Local(w.as_Index()), scope, to_above);
        }
    }
    if (!val.m_root.is_Local()) {
        // No raising of these source values?
        return;
    }
    const auto idx = val.m_root.as_Local();
    bool is_temp = (idx >= m_first_temp_idx);
    /*
    if( !is_temp ) {
        return ;
    }
    */

    // Find controlling scope
    auto scope_it = m_scope_stack.rbegin();
    while (scope_it != m_scope_stack.rend()) {
        auto& scope_def = m_scopes.at(*scope_it);

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
    if (scope_it == m_scope_stack.rend()) {
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
    for (; scope_it != m_scope_stack.rend(); ++scope_it) {
        auto& scope_def = m_scopes.at(*scope_it);
        DEBUG("> Cross " << *scope_it << " - " << scope_def.data.tag_str());

        if (*scope_it == scope.idx) {
            target_seen = true;
        }

        TU_MATCH_HDRA((scope_def.data), {)
        TU_ARMA(Owning, e) {
                if (target_seen && e.is_temporary == is_temp) {
                    e.slots.push_back(idx);
                    DEBUG("- to " << *scope_it);
                    return;
                }
            }
            TU_ARMA(Loop, sd_loop) {
                // If there is an exit state present, ensure that this variable is
                // present in that state (as invalid, as it can't have been valid
                // externally)
                if (sd_loop.exit_state_valid) {
                    DEBUG("Adding " << val << " as unset to loop exit state");
                    auto v = sd_loop.exit_state.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "Raising " << val << " which already had a state entry");
                } else {
                    DEBUG("Crossing loop with no existing exit state");
                }
            }
            TU_ARMA(Split, sd_split) {
                // If the split has already registered an exit state, ensure that
                // this variable is present in it. (as invalid)
                if (sd_split.end_state_valid) {
                    DEBUG("Adding " << val << " as unset to loop exit state");
                    auto v = sd_split.end_state.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "Raising " << val << " which already had a state entry");
                } else {
                    DEBUG("Crossing split with no existing end state");
                }

                // TODO: This should update the outer state to unset.
                auto& arm = sd_split.arms.back();
                arm.states.insert(::std::make_pair(idx, get_slot_state(sp, idx, SlotType::Local).clone()));
                m_slot_states.at(idx) = VarState(InvalidType::Uninit);
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

MirBuilder::SaveCodeProto MirBuilder::code_save_start() {
    TRACE_FUNCTION;
    // Push to the stack
    // Create a new block and link in
    static size_t s_next_index;
    SaveCodeProto rv;
    rv.index = s_next_index++;
    m_code_save_stack.push_back(CodeSaveStackEnt{rv.index, {}});
    // If currently in a block, then go into a new one
    if (block_active()) {
        new_bb_linked();
    }
    return rv;
}

MirBuilder::SavedCode MirBuilder::code_save_end(SaveCodeProto h) {
    // Check stack
    assert(!block_active()); // Can't be a block active
    assert(!m_code_save_stack.empty());
    assert(h.index == m_code_save_stack.back().index);
    SavedCode rv;
    rv.blocks = std::move(m_code_save_stack.back().blocks);
    m_code_save_stack.pop_back();
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
        } cloner{sp, mapper, m_resolve.m_crate.m_types};

        // Allocate new block IDs for all referenced blocks
        for (auto bb_idx : c.blocks) {
            cloner.new_block_map.insert(std::make_pair(bb_idx, new_bb_unlinked()));
        }
        // End the current block with a goto to the first block
        end_block(::MIR::Terminator::make_Goto({cloner.new_block_map[c.blocks.front()]}));

        DEBUG("c.blocks = [" << c.blocks << "]");
        DEBUG("new_block_map = {" << cloner.new_block_map << "}");
        // Start inserting (and remapping)
        for (auto src_idx : c.blocks) {
            auto new_idx = cloner.new_block_map.at(src_idx);
            DEBUG("BB" << new_idx << " <= BB" << src_idx);
            const auto& src = m_output.blocks[src_idx];
            set_cur_block(new_idx);
            for (const auto& v : src.statements) {
                push_stmt(sp, cloner.clone_stmt(v));
            }
            end_block(cloner.clone_term(src.terminator));
        }
        // Leave no active block
    }
}

void MirBuilder::set_cur_block(unsigned int new_block) {
    ASSERT_BUG(Span(), !m_block_active, "Updating block when previous is active");
    ASSERT_BUG(Span(), new_block < m_output.blocks.size(), "Invalid block ID being started - " << new_block);
    ASSERT_BUG(Span(), m_output.blocks[new_block].terminator.is_Incomplete(), "Attempting to resume a completed block - BB" << new_block);
    // Record this new block in the save stack entries
    for (auto& v : m_code_save_stack) {
        // Just in case a block is saved+resumed
        if (std::find(v.blocks.begin(), v.blocks.end(), new_block) == v.blocks.end()) {
            v.blocks.push_back(new_block);
        }
    }
    DEBUG("BB" << new_block << " START");
    m_current_block = new_block;
    m_block_active = true;
}

void MirBuilder::end_block(::MIR::Terminator term) {
    if (!m_block_active) {
        BUG(Span(), "Terminating block when none active");
    }
    DEBUG("BB" << m_current_block << " END -> " << term);
    m_output.blocks.at(m_current_block).terminator = mv$(term);
    m_block_active = false;
    m_current_block = 0;
}

::MIR::BasicBlockId MirBuilder::pause_cur_block() {
    if (!m_block_active) {
        BUG(Span(), "Pausing block when none active");
    }
    DEBUG("BB" << m_current_block << " PAUSE");
    m_block_active = false;
    auto rv = m_current_block;
    m_current_block = 0;
    return rv;
}

::MIR::BasicBlockId MirBuilder::new_bb_linked() {
    auto rv = new_bb_unlinked();
    DEBUG("BB" << rv);
    end_block(::MIR::Terminator::make_Goto(rv));
    set_cur_block(rv);
    return rv;
}

::MIR::BasicBlockId MirBuilder::new_bb_unlinked() {
    auto rv = m_output.blocks.size();
    DEBUG("BB" << rv);
    m_output.blocks.push_back({});
    return rv;
}

unsigned int MirBuilder::new_drop_flag(bool default_state) {
    auto rv = m_output.drop_flags.size();
    m_output.drop_flags.push_back(default_state);
    for (size_t i = m_scope_stack.size(); i--;) {
        if (auto* e = m_scopes.at(m_scope_stack[i]).data.opt_Loop()) {
            e->drop_flags.push_back(rv);
            break;
        }
    }
    DEBUG("df$" << rv << " := " << default_state);
    return rv;
}

unsigned int MirBuilder::new_drop_flag_and_set(const Span& sp, bool set_state) {
    auto rv = new_drop_flag(!set_state);
    push_stmt_set_dropflag_val(sp, rv, set_state);
    return rv;
}

bool MirBuilder::get_drop_flag_default(const Span& sp, unsigned int idx) {
    return m_output.drop_flags.at(idx);
}

void MirBuilder::drop_flag_alias(unsigned int old_idx, unsigned int new_idx) {
    m_drop_flag_aliases[old_idx].push_back(new_idx);
}

ScopeHandle MirBuilder::new_scope_var(const Span& sp) {
    unsigned int idx = m_scopes.size();
    m_scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({false, {}})});
    m_scope_stack.push_back(idx);
    DEBUG("START (var) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::new_scope_temp(const Span& sp) {
    unsigned int idx = m_scopes.size();

    m_scopes.push_back(ScopeDef{sp, ScopeType::make_Owning({true, {}})});
    m_scope_stack.push_back(idx);
    DEBUG("START (temp) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::new_scope_split(const Span& sp) {
    unsigned int idx = m_scopes.size();
    m_scopes.push_back(ScopeDef{sp, ScopeType::make_Split({})});
    m_scopes.back().data.as_Split().arms.push_back({});
    m_scope_stack.push_back(idx);
    DEBUG("START (split) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::new_scope_loop(const Span& sp) {
    unsigned int idx = m_scopes.size();
    m_scopes.push_back(ScopeDef{sp, ScopeType::make_Loop({})});
    m_scopes.back().data.as_Loop().entry_bb = m_current_block;
    m_scope_stack.push_back(idx);
    DEBUG("START (loop) scope " << idx);
    return ScopeHandle{*this, idx};
}

ScopeHandle MirBuilder::new_scope_freeze(const Span& sp) {
    unsigned int idx = m_scopes.size();
    m_scopes.push_back(ScopeDef{sp, ScopeType::make_Freeze({})});
    m_scope_stack.push_back(idx);
    DEBUG("START (freeze) scope " << idx);
    return ScopeHandle{*this, idx};
}

void MirBuilder::terminate_scope(const Span& sp, ScopeHandle scope, bool emit_cleanup /*=true*/) {
    TRACE_FUNCTION_F("DONE scope " << scope.idx << " - " << (emit_cleanup ? "CLEANUP" : "NO CLEANUP"));
    // 1. Check that this is the current scope (at the top of the stack)
    if (m_scope_stack.empty() || m_scope_stack.back() != scope.idx) {
        DEBUG("- m_scope_stack = [" << m_scope_stack << "]");
        auto it = ::std::find(m_scope_stack.begin(), m_scope_stack.end(), scope.idx);
        if (it == m_scope_stack.end()) {
            BUG(sp, "Terminating scope not on the stack - scope " << scope.idx);
        }
        BUG(sp, "Terminating scope " << scope.idx << " when not at top of stack, " << (m_scope_stack.end() - it - 1) << " scopes in the way");
    }

    auto& scope_def = m_scopes.at(scope.idx);
    //if( emit_cleanup ) {
    //    ASSERT_BUG( sp, scope_def.complete == false, "Terminating scope which is already terminated" );
    //}

    if (emit_cleanup && scope_def.complete == false) {
        // 2. Emit drops for all non-moved variables (share with below)
        drop_scope_values(scope_def);

// Emit ScopeEnd for all controlled values
    }

    // 3. Pop scope (last because `drop_scope_values` uses the stack)
    m_scope_stack.pop_back();

    complete_scope(scope_def);
}

void MirBuilder::raise_all(const Span& sp, ScopeHandle source, const ScopeHandle& target) {
    TRACE_FUNCTION_F("scope " << source.idx << " => " << target.idx);

    // 1. Check that this is the current scope (at the top of the stack)
    if (m_scope_stack.empty() || m_scope_stack.back() != source.idx) {
        DEBUG("- m_scope_stack = [" << m_scope_stack << "]");
        auto it = ::std::find(m_scope_stack.begin(), m_scope_stack.end(), source.idx);
        if (it == m_scope_stack.end()) {
            BUG(sp, "Terminating scope not on the stack - scope " << source.idx);
        }
        BUG(sp, "Terminating scope " << source.idx << " when not at top of stack, " << (m_scope_stack.end() - it - 1) << " scopes in the way");
    }
    auto& src_scope_def = m_scopes.at(source.idx);

    ASSERT_BUG(sp, src_scope_def.data.is_Owning(), "Rasising scopes can only be done on temporaries (source)");
    ASSERT_BUG(sp, src_scope_def.data.as_Owning().is_temporary, "Rasising scopes can only be done on temporaries (source)");
    auto& src_list = src_scope_def.data.as_Owning().slots;
    for (auto idx : src_list) {
        DEBUG("> Raising " << ::MIR::LValue::new_Local(idx));
        assert(idx >= m_first_temp_idx);
    }

    // Seek up stack until the target scope is seen
    auto it = m_scope_stack.rbegin() + 1;
    for (; it != m_scope_stack.rend() && *it != target.idx; ++it) {
        auto& scope_def = m_scopes.at(*it);
        DEBUG("Through S" << *it << ": " << scope_def.data.tag_str());

        if (auto* sd_loop = scope_def.data.opt_Loop()) {
            if (sd_loop->exit_state_valid) {
                DEBUG("Crossing loop with existing end state");
                // Insert these values as Invalid, both in the existing exit state, and in the changed list
                for (auto idx : src_list) {
                    auto v = sd_loop->exit_state.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "");
                }
            } else {
                DEBUG("Crossing loop with no end state");
            }

            for (auto idx : src_list) {
                auto v2 = sd_loop->changed_slots.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                ASSERT_BUG(sp, v2.second, "");
            }
        } else if (auto* sd_split = scope_def.data.opt_Split()) {
            if (sd_split->end_state_valid) {
                DEBUG("Crossing split with existing end state");
                // Insert these indexes as Invalid
                for (auto idx : src_list) {
                    auto v = sd_split->end_state.states.insert(::std::make_pair(idx, VarState(InvalidType::Uninit)));
                    ASSERT_BUG(sp, v.second, "");
                }
            } else {
                DEBUG("Crossing split with no end state");
            }

            // TODO: Insert current state in the current arm
            assert(!sd_split->arms.empty());
            auto& arm = sd_split->arms.back();
            for (auto idx : src_list) {
                arm.states.insert(::std::make_pair(idx, mv$(m_slot_states.at(idx))));
                m_slot_states.at(idx) = VarState(InvalidType::Uninit);
            }
        }
    }
    if (it == m_scope_stack.rend()) {
        BUG(sp, "Moving values to a scope not on the stack - scope " << target.idx);
    }
    auto& tgt_scope_def = m_scopes.at(target.idx);
    DEBUG("To S" << target.idx << ": " << tgt_scope_def.data.tag_str());
    ASSERT_BUG(sp, tgt_scope_def.data.is_Owning(), "Rasising scopes can only be done on temporaries (target)");
    ASSERT_BUG(sp, tgt_scope_def.data.as_Owning().is_temporary, "Rasising scopes can only be done on temporaries (target)");

    // Move all defined variables from one to the other
    auto& tgt_list = tgt_scope_def.data.as_Owning().slots;
    tgt_list.insert(tgt_list.end(), src_list.begin(), src_list.end());

    // Scope completed
    m_scope_stack.pop_back();
    src_scope_def.complete = true;
}

void MirBuilder::terminate_scope_early(const Span& sp, const ScopeHandle& scope, bool loop_exit /*=false*/) {
    TRACE_FUNCTION_F("EARLY scope " << scope.idx);

    // 1. Ensure that this block is in the stack
    auto it = ::std::find(m_scope_stack.begin(), m_scope_stack.end(), scope.idx);
    if (it == m_scope_stack.end()) {
        BUG(sp, "Early-terminating scope not on the stack");
    }
    unsigned int slot = it - m_scope_stack.begin();

    bool is_conditional = false;
    for (unsigned int i = m_scope_stack.size(); i-- > slot;) {
        auto idx = m_scope_stack[i];
        auto& scope_def = m_scopes.at(idx);

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
            drop_scope_values(scope_def);
            complete_scope(scope_def);
        } else {
            // Mark patial within this scope?
            DEBUG("Drop part of scope " << idx);

            // Emit drops for dropped values within this scope
            drop_scope_values(scope_def);
            // Inform the scope that it's been early-exited
            TU_IFLET(ScopeType, scope_def.data, Split, e, e.arms.back().has_early_terminated = true;)
        }
    }

    // Index 0 is the function scope, this only happens when about to return/panic
    if (scope.idx == 0) {
        // Ensure that all arguments are dropped if they were not moved
        for (size_t i = 0; i < m_arg_states.size(); i++) {
            const auto& state = get_slot_state(sp, i, SlotType::Argument);
            this->drop_value_from_state(sp, state, ::MIR::LValue::new_Argument(static_cast<unsigned>(i)));
        }
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
            if (builder.get_drop_flag_default(sp, new_flag)) {
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
                        auto flag_idx = new_state.as_Optional();
                        if (true || builder.get_drop_flag_default(sp, flag_idx) != false) {
                            auto new_flag = builder.new_drop_flag(false);
                            builder.push_stmt_set_dropflag_other(sp, new_flag, flag_idx);
                            old_state = VarState::make_Optional(new_flag);
                        } else {
                            old_state = VarState::make_Optional(flag_idx);
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
                            if (builder.get_drop_flag_default(sp, ose.outer_flag) != false) {
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
                            merge_state(sp, builder, ::MIR::LValue::new_Deref(lv.clone()), *ose.inner_state, *nse.inner_state);
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
                                merge_state(sp, builder, ::MIR::LValue::new_Downcast(lv.clone(), static_cast<unsigned int>(i)), ose.inner_states[i], nse.inner_states[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::new_Field(lv.clone(), i), ose.inner_states[i], nse.inner_states[i]);
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
                        auto flag_idx = new_state.as_Optional();
                        // Was valid, now optional.
                        if (builder.get_drop_flag_default(sp, flag_idx) != true) {
                            // Allocate a new drop flag with a default state of `true` and set it to this flag?
                            auto new_flag = builder.new_drop_flag(true);
                            builder.push_stmt_set_dropflag_other(sp, new_flag, flag_idx);
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
                            if (builder.get_drop_flag_default(sp, ose.outer_flag) != true) {
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
                            merge_state(sp, builder, ::MIR::LValue::new_Deref(lv.clone()), *ose.inner_state, *nse.inner_state);
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
                            auto ilv = ::MIR::LValue::new_Downcast(lv.clone(), 0);
                            for (size_t i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ilv, ose.inner_states[i], nse.inner_states[i]);
                                ilv.inc_Downcast();
                            }
                        } else {
                            auto ilv = ::MIR::LValue::new_Field(lv.clone(), 0);
                            for (unsigned int i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ilv, ose.inner_states[i], nse.inner_states[i]);
                                ilv.inc_Field();
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
                            merge_state(sp, builder, ::MIR::LValue::new_Deref(lv.clone()), *old_state.as_MovedOut().inner_state, *new_state.as_MovedOut().inner_state);
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
                                auto new_flag = builder.new_drop_flag(builder.get_drop_flag_default(sp, old_state.as_Optional()));
                                builder.drop_flag_alias(old_state.as_Optional(), new_flag);
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
                                merge_state(sp, builder, ::MIR::LValue::new_Downcast(lv.clone(), static_cast<unsigned int>(i)), ose.inner_states[i], nse.inner_states[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::new_Field(lv.clone(), i), ose.inner_states[i], nse.inner_states[i]);
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

                        merge_state(sp, builder, ::MIR::LValue::new_Deref(lv.clone()), *ose.inner_state, new_state);
                        return;
                    }
                    case VarState::TAG_Optional: {
                        const auto& nse = new_state.as_Optional();
                        if (ose.outer_flag == ~0u) {
                            if (!builder.get_drop_flag_default(sp, nse)) {
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
                        merge_state(sp, builder, ::MIR::LValue::new_Deref(lv.clone()), *ose.inner_state, new_state);
                        return;
                    }
                    case VarState::TAG_MovedOut: {
                        const auto& nse = new_state.as_MovedOut();

                        if (ose.outer_flag == ~0u) {
                            ose.outer_flag = nse.outer_flag;
                        } else {
                            builder.push_stmt_set_dropflag_other(sp, ose.outer_flag, nse.outer_flag);
                        }
                        merge_state(sp, builder, ::MIR::LValue::new_Deref(lv.clone()), *ose.inner_state, *nse.inner_state);
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
                                merge_state(sp, builder, ::MIR::LValue::new_Downcast(lv.clone(), static_cast<unsigned int>(i)), ose.inner_states[i], new_state);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::new_Field(lv.clone(), i), ose.inner_states[i], new_state);
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
                                merge_state(sp, builder, ::MIR::LValue::new_Downcast(lv.clone(), static_cast<unsigned int>(i)), ose.inner_states[i], nse.inner_states[i]);
                            }
                        } else {
                            for (unsigned int i = 0; i < ose.inner_states.size(); i++) {
                                merge_state(sp, builder, ::MIR::LValue::new_Field(lv.clone(), i), ose.inner_states[i], nse.inner_states[i]);
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
    if (sd_loop.exit_state_valid) {
        // Insert copies of parent state for newly changed values
        // and Merge all changed values
        auto merge_list = [sp, this](const auto& changed, auto& exit_states, ::std::function<::MIR::LValue(unsigned)> val_cb, auto type) {
            for (const auto& ent : changed) {
                auto idx = ent.first;
                auto it = exit_states.find(idx);
                if (it == exit_states.end()) {
                    it = exit_states.insert(::std::make_pair(idx, ent.second.clone())).first;
                }
                auto& old_state = it->second;
                merge_state(sp, *this, val_cb(idx), old_state, get_slot_state(sp, idx, type));
            }
        };
        merge_list(sd_loop.changed_slots, sd_loop.exit_state.states, ::MIR::LValue::new_Local, SlotType::Local);
        merge_list(sd_loop.changed_args, sd_loop.exit_state.arg_states, [](auto v) {
            return ::MIR::LValue::new_Argument(v);
        }, SlotType::Argument);
    } else {
        auto init_list = [sp, this](const auto& changed, auto& exit_states, auto type) {
            for (const auto& ent : changed) {
                DEBUG("Slot(" << ent.first << ") = " << ent.second);
                auto idx = ent.first;
                exit_states.insert(::std::make_pair(idx, get_slot_state(sp, idx, type).clone()));
            }
        };
        // Obtain states of changed variables/temporaries
        init_list(sd_loop.changed_slots, sd_loop.exit_state.states, SlotType::Local);
        init_list(sd_loop.changed_args, sd_loop.exit_state.arg_states, SlotType::Argument);
        sd_loop.exit_state_valid = true;
    }
}

void MirBuilder::merge_split_lists(const Span& sp, const ScopeHandle& handle, const ::std::map<unsigned int, VarState>& states, ::std::map<unsigned int, VarState>& end_states, MirBuilder::SlotType type) {
    // Insert copies of the parent state
    for (const auto& ent : states) {
        if (end_states.count(ent.first) == 0) {
            auto s = this->get_slot_state(sp, ent.first, type, &handle).clone();
            DEBUG("Add from parent: " << (type == SlotType::Local ? ::MIR::LValue::new_Local(ent.first) : ::MIR::LValue::new_Argument(ent.first)) << " = " << s);
            end_states.insert(::std::make_pair(ent.first, std::move(s)));
        }
    }
    // Merge state
    for (auto& ent : end_states) {
        auto idx = ent.first;
        auto& out_state = ent.second;

        // Merge the states
        auto it = states.find(idx);
        const auto& src_state = (it != states.end() ? it->second : this->get_slot_state(sp, idx, type, &handle));

        auto lv = (type == SlotType::Local ? ::MIR::LValue::new_Local(idx) : ::MIR::LValue::new_Argument(idx));
        merge_state(sp, *this, mv$(lv), out_state, src_state);
    }
}

void MirBuilder::end_split_arm(const Span& sp, const ScopeHandle& handle, bool reachable, bool early /*=false*/) {
    ASSERT_BUG(sp, handle.idx < m_scopes.size(), "Handle passed to end_split_arm is invalid");
    auto& sd = m_scopes.at(handle.idx);
    ASSERT_BUG(sp, sd.data.is_Split(), "Ending split arm on non-Split arm - " << sd.data.tag_str());
    auto& sd_split = sd.data.as_Split();
    ASSERT_BUG(sp, !sd_split.arms.empty(), "Split arm list is empty (impossible)");

    // If this is not at the top of the stack (if there are other splits in the way), then get state from them
    for (auto v : ::reverse(m_scope_stack)) {
        if (v == handle.idx) {
            break;
        }

        // If this stack entry is a Split, get the current values and add them to `sd_split`
        if (const auto* other_split = m_scopes.at(v).data.opt_Split()) {
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
        ASSERT_BUG(sp, m_block_active, "Block must be active when ending a reachable split arm");
    }

    auto& this_arm_state = sd_split.arms.back();
    this_arm_state.always_early_terminated = /*sd_split.arms.back().has_early_terminated &&*/ !reachable;

    if (sd_split.end_state_valid) {
        if (reachable) {
            DEBUG("Reachable w/ end state, merging");

            merge_split_lists(sp, handle, this_arm_state.states, sd_split.end_state.states, SlotType::Local);
            merge_split_lists(sp, handle, this_arm_state.arg_states, sd_split.end_state.arg_states, SlotType::Argument);
        } else {
            DEBUG("Unreachable, not merging");
        }
    } else {
        if (reachable) {
            DEBUG("Reachable w/ no end state, setting");
            // Clone this arm's state
            for (auto& ent : this_arm_state.states) {
                DEBUG("State _" << ent.first << " = " << ent.second);
                sd_split.end_state.states.insert(::std::make_pair(ent.first, ent.second.clone()));
            }
            for (auto& ent : this_arm_state.arg_states) {
                DEBUG("State a" << ent.first << " = " << ent.second);
                sd_split.end_state.arg_states.insert(::std::make_pair(ent.first, ent.second.clone()));
            }
            sd_split.end_state_valid = true;
        } else {
            DEBUG("Unreachable, not setting");
        }
    }

    if (reachable) {
        assert(m_block_active);
    }
    if (!early) {
        SplitArm arm;
        DEBUG("New Arm");
        for (auto& ent : sd_split.cond_state.states) {
            DEBUG("Condition State _" << ent.first << " = " << ent.second);
            arm.states.insert(::std::make_pair(ent.first, ent.second.clone()));
        }
        for (auto& ent : sd_split.cond_state.arg_states) {
            DEBUG("Condition State a" << ent.first << " = " << ent.second);
            arm.arg_states.insert(::std::make_pair(ent.first, ent.second.clone()));
        }
        sd_split.arms.push_back(mv$(arm));
    }
}

void MirBuilder::end_split_arm_early(const Span& sp) {
    TRACE_FUNCTION_F("");
    size_t i = m_scope_stack.size();
    // Terminate every sequence of owning scopes
    while (i-- && m_scopes.at(m_scope_stack[i]).data.is_Owning()) {
        auto& scope_def = m_scopes[m_scope_stack[i]];
        // Fully drop the scope
        DEBUG("Complete scope " << m_scope_stack[i]);
        drop_scope_values(scope_def);
        complete_scope(scope_def);
    }

    if (i < m_scope_stack.size()) {
        if (m_scopes.at(m_scope_stack[i]).data.is_Split()) {
            DEBUG("Early terminate split scope " << m_scope_stack.back());
            auto& sd = m_scopes[m_scope_stack[i]];
            auto& sd_split = sd.data.as_Split();
            sd_split.arms.back().has_early_terminated = true;

            // TODO: Create drop flags if required?
        }
        // TODO: What if this is a loop?
    }
}

void MirBuilder::end_split_condition(const Span& sp, const ScopeHandle& handle) {
    ASSERT_BUG(sp, handle.idx < m_scopes.size(), "Handle passed to end_split_arm is invalid");
    auto& sd = m_scopes.at(handle.idx);
    ASSERT_BUG(sp, sd.data.is_Split(), "Ending split arm on non-Split arm - " << sd.data.tag_str());
    auto& sd_split = sd.data.as_Split();
    ASSERT_BUG(sp, !sd_split.arms.empty(), "Split arm list is empty (impossible)");

    const auto& this_arm_state = sd_split.arms.back();

    DEBUG("Split condition clause end (scope " << handle.idx << "): merging");

    merge_split_lists(sp, handle, this_arm_state.states, sd_split.cond_state.states, SlotType::Local);
    merge_split_lists(sp, handle, this_arm_state.arg_states, sd_split.cond_state.arg_states, SlotType::Argument);
}

void MirBuilder::unfreeze_scope(const Span& sp, const ScopeHandle& handle) {
    ASSERT_BUG(sp, handle.idx < m_scopes.size(), "Handle passed to `unfreeze_scope` is invalid");
    auto& sd = m_scopes.at(handle.idx);
    ASSERT_BUG(sp, sd.data.is_Freeze(), "Handle passed to `unfreeze_scope` was not a freeze,  - " << sd.data.tag_str());
    auto& sd_e = sd.data.as_Freeze();

    DEBUG("Unfreeze scope " << handle.idx);
    sd_e.unfrozen = true;
}

void MirBuilder::complete_scope(ScopeDef& sd) {
    struct H {
        static void apply_end_state(const Span& sp, MirBuilder& builder, SplitEnd& end_state) {
            for (auto& ent : end_state.states) {
                auto& vs = builder.get_slot_state_mut(sp, ent.first, SlotType::Local);
                if (vs != ent.second) {
                    DEBUG(::MIR::LValue::new_Local(ent.first) << " " << vs << " => " << ent.second);
                    vs = ::std::move(ent.second);
                }
            }
            for (auto& ent : end_state.arg_states) {
                auto& vs = builder.get_slot_state_mut(sp, ent.first, SlotType::Argument);
                if (vs != ent.second) {
                    DEBUG(::MIR::LValue::new_Argument(ent.first) << " " << vs << " => " << ent.second);
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
            if (e.exit_state_valid) {
                H::apply_end_state(sd.span, *this, e.exit_state);
            }

            // Insert sets of drop flags to the first block (at the start of that block)
            auto& stmts = m_output.blocks.at(e.entry_bb).statements;
            for (auto idx : e.drop_flags) {
                DEBUG("Reset df$" << idx);
                stmts.insert(stmts.begin(), ::MIR::Statement::make_SetDropFlag({idx, m_output.drop_flags.at(idx), ~0u}));
            }
        }
        TU_ARMA(Split, e) {
            TRACE_FUNCTION_F("Split - " << (e.arms.size() - 1) << " arms");

            // TODO: if not set, then end the current state as unreachable?
            //ASSERT_BUG(sd.span, e.end_state_valid, "Completing split scope with no end state set?");
            if (e.end_state_valid) {
                H::apply_end_state(sd.span, *this, e.end_state);
            }
        }
    }
}

void MirBuilder::with_val_type(const Span& sp, const ::MIR::LValue& val, ::std::function<void(const ::HIR::TypeRef&)> cb, const ::MIR::LValue::Wrapper* stop_wrapper /*=nullptr*/) const {
    ::HIR::TypeRef tmp;
    const ::HIR::TypeRef* ty_p = nullptr;
    TU_MATCHA((val.m_root), (e), (Return, ty_p = &m_ret_ty;), (Argument, ty_p = &m_args.at(e).second;), (Local, ty_p = &m_output.locals.at(e);), (Static, TU_MATCHA((e.m_data), (pe), (Generic, ASSERT_BUG(sp, pe.m_params.m_types.empty(), "Path params on static"); const auto& s = m_resolve.m_crate.get_static_by_path(sp, pe.m_path); ty_p = &s.m_type;), (UfcsKnown, TODO(sp, "Static - UfcsKnown - " << e);), (UfcsUnknown, BUG(sp, "Encountered UfcsUnknown in Static - " << e);), (UfcsInherent, TODO(sp, "Static - UfcsInherent - " << e);))))
    assert(ty_p);
    for (const auto& w : val.m_wrappers) {
        if (&w == stop_wrapper) {
            stop_wrapper = nullptr; // Reset so the below bugcheck can work
            break;
        }
        const auto& ty = *ty_p;
        ty_p = nullptr;
        //DEBUG(ty << " " << w);
        auto maybe_monomorph = [&](const ::HIR::GenericParams& params_def, const ::HIR::Path& p, const ::HIR::TypeRef& t) -> const ::HIR::TypeRef& {
            if (monomorphise_type_needed(t)) {
                tmp = MonomorphStatePtr(m_resolve.m_crate.m_types, nullptr, &p.m_data.as_Generic().m_params, nullptr).monomorph_type(sp, t);
                m_resolve.expand_associated_types(sp, tmp);
                return tmp;
            } else {
                return t;
            }
        };
        TU_MATCH_HDRA( (w), {)
        TU_ARMA(Field, field_index) {
            TU_MATCH_HDRA( (*ty), {)
            default:
                BUG(sp, "Field access on unexpected type - " << ty);
                    TU_ARMA(Array, te) {
                        ty_p = &te.inner;
                    }
                    TU_ARMA(Slice, te) {
                        ty_p = &te.inner;
                    }
                    TU_ARMA(Path, te) {
                        if (const auto* tep = te.binding.opt_Struct()) {
                            const auto& str = **tep;
                            TU_MATCHA((str.m_data), (se), (Unit, BUG(sp, "Field on unit-like struct - " << ty);), (Tuple, ASSERT_BUG(sp, field_index < se.size(), "Field index out of range in tuple-struct " << ty << " - " << field_index << " > " << se.size()); const auto& fld = se[field_index]; ty_p = &maybe_monomorph(str.m_params, te.path, fld.ent);), (Named, ASSERT_BUG(sp, field_index < se.size(), "Field index out of range in struct " << ty << " - " << field_index << " > " << se.size()); const auto& fld = se[field_index]; ty_p = &maybe_monomorph(str.m_params, te.path, fld.ty);))
                        } else if (/*const auto* tep =*/te.binding.opt_Union()) {
                            BUG(sp, "Field access on a union isn't valid, use Downcast instead - " << ty);
                        } else {
                            BUG(sp, "Field acess on unexpected type - " << ty);
                        }
                    }
                    TU_ARMA(Tuple, te) {
                        ASSERT_BUG(sp, field_index < te.size(), "Field index out of range in tuple " << field_index << " >= " << te.size());
                        ty_p = &te[field_index];
                    }
            }
            }
            TU_ARMA(Deref, _e) {
            TU_MATCH_HDRA( (*ty), { )
            default:
                BUG(sp, "Deref on unexpected type - " << ty);
                    TU_ARMA(Path, te) {
                        if (const auto* inner_ptr = this->is_type_owned_box(ty)) {
                            ty_p = &*inner_ptr;
                        } else {
                            BUG(sp, "Deref on unexpected type - " << ty);
                        }
                    }
                    TU_ARMA(Pointer, te) {
                        ty_p = &te.inner;
                    }
                    TU_ARMA(Borrow, te) {
                        ty_p = &te.inner;
                    }
            }
            }
            TU_ARMA(Index, _index_val) {
                TU_MATCH_DEF(::HIR::TypeData, (*ty), (te), (BUG(sp, "Index on unexpected type - " << ty);), (Slice, ty_p = &te.inner;), (Array, ty_p = &te.inner;))
            }
            TU_ARMA(Downcast, variant_index) {
            TU_MATCH_HDRA( (*ty), { )
            default:
                BUG(sp, "Downcast on unexpected type - " << ty);
                    TU_ARMA(Path, te) {
                        if (const auto* pbe = te.binding.opt_Enum()) {
                            const auto& enm = **pbe;
                            ASSERT_BUG(sp, enm.m_data.is_Data(), "Downcast on non-data enum");
                            const auto& variants = enm.m_data.as_Data();
                            ASSERT_BUG(sp, variant_index < variants.size(), "Variant index out of range");
                            const auto& variant = variants[variant_index];

                            ty_p = &maybe_monomorph(enm.m_params, te.path, variant.type);
                        } else if (const auto* pbe = te.binding.opt_Union()) {
                            const auto& unm = **pbe;
                            ASSERT_BUG(sp, variant_index < unm.m_variants.size(), "Variant index out of range");
                            const auto& variant = unm.m_variants.at(variant_index);

                            ty_p = &maybe_monomorph(unm.m_params, te.path, variant.ty);
                        } else {
                            BUG(sp, "Downcast on non-Enum/Union - " << ty << " for " << val);
                        }
                    }
            }
            }
        }
        assert(ty_p);
    }
    ASSERT_BUG(sp, !stop_wrapper, "A stop wrapper was passed, but not found");
    cb(*ty_p);
}

bool MirBuilder::lvalue_is_copy(const Span& sp, const ::MIR::LValue& val) const {
    int rv = 0;
    with_val_type(sp, val, [&](const auto& ty) {
        DEBUG("[lvalue_is_copy] ty=" << ty);
        rv = (m_resolve.type_is_copy(sp, ty) ? 2 : 1);
    });
    ASSERT_BUG(sp, rv != 0, "Type for " << val << " can't be determined");
    return rv == 2;
}

const VarState& MirBuilder::get_slot_state(const Span& sp, unsigned int idx, SlotType type, const ScopeHandle* above_scope /*=nullptr*/) const {
    // 1. Find an applicable Split scope
    for (auto scope_idx : ::reverse(m_scope_stack)) {
        // Is this supposed to only consider above a specified (likely split) scope?
        if (above_scope) {
            // Once the scope is found, clear `above_scope` so subsequent iterations skip this check
            if (scope_idx == above_scope->idx) {
                above_scope = nullptr;
            }
            continue;
        }
        const auto& scope_def = m_scopes.at(scope_idx);
        TU_MATCH_HDRA( (scope_def.data), {)
        default:
            break;
            TU_ARMA(Owning, e) {
                if (type == SlotType::Local) {
                    auto it = ::std::find(e.slots.begin(), e.slots.end(), idx);
                    if (it != e.slots.end()) {
                        break;
                    }
                }
            }
            TU_ARMA(Split, e) {
                const auto& cur_arm = e.arms.back();
                const auto& list = (type == SlotType::Local ? cur_arm.states : cur_arm.arg_states);
                auto it = list.find(idx);
                if (it != list.end()) {
                    DEBUG("From scope " << scope_idx);
                    return it->second;
                }
            }
        }
    }

    if (above_scope) {
        BUG(sp, "Scope " << *above_scope << " not found on stack");
    }
    switch (type) {
        case SlotType::Local:
            if (idx == ~0u) {
                return m_return_state;
            } else {
                ASSERT_BUG(sp, idx < m_slot_states.size(), "Slot " << idx << " out of range for state table");
                return m_slot_states.at(idx);
            }
            break;
        case SlotType::Argument:
            ASSERT_BUG(sp, idx < m_arg_states.size(), "Argument " << idx << " out of range for state table");
            return m_arg_states.at(idx);
    }
    throw "";
}

VarState& MirBuilder::get_slot_state_mut(const Span& sp, unsigned int idx, SlotType type) {
    VarState* ret = nullptr;
    for (auto scope_idx : ::reverse(m_scope_stack)) {
        auto& scope_def = m_scopes.at(scope_idx);
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
                auto& cur_arm = e.arms.back();
                if (!ret) {
                    if (idx == ~0u) {
                    } else {
                        auto& states = (type == SlotType::Local ? cur_arm.states : cur_arm.arg_states);
                        auto it = states.find(idx);
                        if (it == states.end()) {
                            DEBUG("Split new (scope " << scope_idx << ")");
                            it = states.insert(::std::make_pair(idx, get_slot_state(sp, idx, type).clone())).first;
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
                    auto& states = (type == SlotType::Local ? e.changed_slots : e.changed_args);
                    if (states.count(idx) == 0) {
                        auto state = e.exit_state_valid ? get_slot_state(sp, idx, type).clone() : VarState::make_Valid({});
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
                ret = (idx == ~0u) ? &m_return_state : &m_slot_states.at(idx);
                break;
            case SlotType::Argument:
                ret = &m_arg_states.at(idx);
                break;
        }
    }
    assert(ret);
    return *ret;
}

VarState* MirBuilder::get_val_state_mut_p(const Span& sp, const ::MIR::LValue& lv, bool expect_valid /*=false*/) {
    TRACE_FUNCTION_F(lv);
    VarState* vs = nullptr;
    TU_MATCHA(
        (lv.m_root),
        (e),
        (Return, BUG(sp, "Move of return value"); vs = &get_slot_state_mut(sp, ~0u, SlotType::Local);),
        (Argument, vs = &get_slot_state_mut(sp, e, SlotType::Argument);),
        (Local, vs = &get_slot_state_mut(sp, e, SlotType::Local);),
        (
            Static, return nullptr;
            //BUG(sp, "Attempting to mutate state of a static");
        )
    )
    assert(vs);

    if (expect_valid && vs->is_Valid()) {
        return nullptr;
    }

    for (const auto& w : lv.m_wrappers) {
        auto& ivs = *vs;
        vs = nullptr;
        TU_MATCH_HDRA( (w), { )
        TU_ARMA(Field, field_index) {
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
                            TU_MATCHA((str.m_data), (se), (Unit, BUG(sp, "Field access of unit-like struct");), (Tuple, n_flds = se.size();), (Named, n_flds = se.size();))
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
                vs = &ivs.as_Partial().inner_states.at(field_index);
            }
            TU_ARMA(Deref, _e) {
                // HACK: If the dereferenced type is a Box ("owned_box") then hack in move and shallow drop
                bool is_box = false;
                if (this->m_lang_Box) {
                    with_val_type(sp, lv, [&](const auto& ty) {
                        DEBUG("ty = " << ty);
                        is_box = this->is_type_owned_box(ty);
                    }, &w);
                }

                if (is_box) {
                    if (!ivs.is_MovedOut()) {
                        ::std::vector<VarState> inner;
                        inner.push_back(VarState::make_Valid({}));
                        unsigned int drop_flag = (ivs.is_Optional() ? ivs.as_Optional() : ~0u);
                        ivs = VarState::make_MovedOut({box$(VarState::make_Valid({})), drop_flag});
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
                            var_count = unm.m_variants.size();
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

void MirBuilder::drop_value_from_state(const Span& sp, const VarState& vs, ::MIR::LValue lv) {
    TRACE_FUNCTION_F(lv << " " << vs);
    TU_MATCHA(
        (vs),
        (vse),
        (Invalid, ),
        (Valid, push_stmt_drop(sp, mv$(lv));),
        (
            MovedOut, bool is_box = false; with_val_type(
                sp,
                lv,
                [&](const auto& ty) {
        is_box = this->is_type_owned_box(ty);
    }
            );
            if (is_box) {
                drop_value_from_state(sp, *vse.inner_state, ::MIR::LValue::new_Deref(lv.clone()));
                push_stmt_drop_shallow(sp, mv$(lv), vse.outer_flag);
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
                bool has_valid_variant = false;
                for (const auto& state : vse.inner_states) {
                    has_valid_variant |= !state.is_Invalid();
                }
                if (!has_valid_variant) {
                    return;
                }

                const auto next_bb = new_bb_unlinked();
                ::std::vector<::MIR::BasicBlockId> arms;
                ::std::vector<::MIR::BasicBlockId> cleanup_blocks;
                arms.reserve(vse.inner_states.size());
                cleanup_blocks.reserve(vse.inner_states.size());
                for (const auto& state : vse.inner_states) {
                    const auto cleanup_bb = state.is_Invalid() ? next_bb : new_bb_unlinked();
                    arms.push_back(cleanup_bb);
                    cleanup_blocks.push_back(cleanup_bb);
                }
                end_block(::MIR::Terminator::make_Switch({lv.clone(), mv$(arms), vse.outer_flag, vse.outer_flag == ~0u ? ~0u : next_bb}));

                for (size_t i = 0; i < vse.inner_states.size(); i++) {
                    if (vse.inner_states[i].is_Invalid()) {
                        continue;
                    }
                    set_cur_block(cleanup_blocks[i]);
                    drop_value_from_state(sp, vse.inner_states[i], ::MIR::LValue::new_Downcast(lv.clone(), static_cast<unsigned int>(i)));
                    end_block(::MIR::Terminator::make_Goto(next_bb));
                }
                set_cur_block(next_bb);
            } else if (is_union) {
                // NOTE: Unions don't drop inner items.
            } else {
                for (size_t i = 0; i < vse.inner_states.size(); i++) {
                    drop_value_from_state(sp, vse.inner_states[i], ::MIR::LValue::new_Field(lv.clone(), static_cast<unsigned int>(i)));
                }
            }
        ),
        (Optional, push_stmt_drop(sp, mv$(lv), vse);)
    )
}

void MirBuilder::drop_scope_values(const ScopeDef& sd) {
    TU_MATCHA(
        (sd.data),
        (e),
        (Owning,
         for (auto idx : ::reverse(e.slots)) {
             const auto& vs = get_slot_state(sd.span, idx, SlotType::Local);
             DEBUG("_" << idx << " - " << vs);
             drop_value_from_state(sd.span, vs, ::MIR::LValue::new_Local(idx));
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
        auto* vs_p = get_val_state_mut_p(sp, lv);
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

::MIR::LValue MirBuilder::get_ptr_to_dst(const Span& sp, const ::MIR::LValue& lv) const {
    // Undo field accesses
    size_t count = 0;
    while (count < lv.m_wrappers.size() && lv.m_wrappers[lv.m_wrappers.size() - 1 - count].is_Field()) {
        count++;
    }

    // TODO: Enum variants?

    ASSERT_BUG(sp, count < lv.m_wrappers.size() && lv.m_wrappers[lv.m_wrappers.size() - 1 - count].is_Deref(), "Access of an unsized field without a dereference - " << lv);

    return lv.clone_unwrapped(count + 1);
}

std::map<unsigned, MirBuilder::SavedActiveLocal> MirBuilder::get_active_locals(const Span& sp, std::set<unsigned>& saved_drop_flags) const {
    TRACE_FUNCTION;
    std::map<unsigned, MirBuilder::SavedActiveLocal> rv;
    for (size_t i = 0; i < m_slot_states.size(); i++) {
        const auto& s = get_slot_state(sp, i, SlotType::Local);
        TU_MATCH_HDRA( (s), {)
        default:
            DEBUG("_" << i << " : " << s);
            s.get_used_drop_flags(&saved_drop_flags);
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

void MirBuilder::drop_actve_local(const Span& sp, ::MIR::LValue lv, const SavedActiveLocal& loc) {
    this->drop_value_from_state(sp, loc.state, mv$(lv));
}

void MirBuilder::emit_unwind_cleanup(const Span& sp) {
    for (auto it = m_scope_stack.rbegin(); it != m_scope_stack.rend(); ++it) {
        drop_scope_values(m_scopes.at(*it));
    }

    for (size_t i = 0; i < m_arg_states.size(); i++) {
        const auto& state = get_slot_state(sp, i, SlotType::Argument);
        drop_value_from_state(sp, state, ::MIR::LValue::new_Argument(static_cast<unsigned>(i)));
    }
}

// --------------------------------------------------------------------

ScopeHandle::~ScopeHandle() {
    if (idx != ~0u) {
        try {
            ASSERT_BUG(Span(), m_builder.m_scopes.size() > idx, "Scope invalid");
            ASSERT_BUG(Span(), m_builder.m_scopes.at(idx).complete, "Scope " << idx << " not completed");
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

bool VarState::get_used_drop_flags(std::set<unsigned>* out) const {
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
                rv |= vs.get_used_drop_flags(out);
            }
        }
        TU_ARMA(MovedOut, ve) {
            if (ve.outer_flag != ~0u) {
                if (out) {
                    out->insert(ve.outer_flag);
                }
                rv = true;
            }
            rv |= ve.inner_state->get_used_drop_flags(out);
        }
    }
    return rv;
}
