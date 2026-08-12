#include "hir_typeck_main_bindings.h"

#include "hir_visitor.h"
#include "hir_expr.h"
#include "hir_typeck_static.h"
#include "hir_typeck_main_bindings.h"
#include <algorithm>
#include "hir_hir.h"

namespace {
    typedef ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>> t_args;

    class ExprVisitorValidate: public ::HIR::ExprVisitor {
        const StaticTraitResolve& m_resolve;
        //const t_args&   m_args;
        const ::HIR::TypeData* real_ret_type;
        ::HIR::TypeRef ret_type;

        struct RetTarget {
            const ::HIR::TypeData* ret_type;
            const ::HIR::TypeData* yield_type;

            RetTarget(const ::HIR::TypeData* ret_type)
                : ret_type(ret_type)
                , yield_type(nullptr)
            {
            }

            RetTarget(const ::HIR::TypeData* ret_type, const ::HIR::TypeData* yield_type)
                : ret_type(ret_type)
                , yield_type(yield_type)
            {
            }
        };

        ::std::vector<RetTarget> closure_ret_types;
        ::std::vector<const ::HIR::ExprNodeLoop*> m_loops;
        //const ::HIR::ExprPtr* m_cur_expr;

        ::HIR::SimplePath m_lang_Index;

    public:
        bool expand_erased_types;

        ExprVisitorValidate(const StaticTraitResolve& res, const t_args& args, const ::HIR::TypeData* ret_type)
            : m_resolve(res)
            ,
            //m_args(args),
            real_ret_type(ret_type)
            , expand_erased_types(true)
        {
            m_lang_Index = m_resolve.m_crate.get_lang_item_path_opt("index");
        }

        void visit_root(::HIR::ExprPtr& node_ptr) {
            const auto& sp = node_ptr->span();

            // Monomorphise erased type
            ret_type = clone_ty_with(m_resolve.m_crate.m_types, sp, real_ret_type, [&](const auto& tpl, auto& rv) -> bool {
                if (const auto* e = tpl->opt_ErasedType()) {
                    if (const auto* ee = e->m_inner.opt_Fcn()) {
                        ASSERT_BUG(sp, ee->m_index < node_ptr.m_erased_types.size(), "Erased type index OOB - " << ee->m_origin << " " << ee->m_index << " >= " << node_ptr.m_erased_types.size());
                        // TODO: Check that erased type bounds are still met
                        rv = node_ptr.m_erased_types[ee->m_index];
                        return true;
                    }
                }
                return false;
            });
            m_resolve.expand_associated_types(sp, ret_type);

            node_ptr->visit(*this);

            check_types_equal(sp, ret_type, node_ptr->m_res_type);
        }

        void visit(::HIR::ExprNodeBlock& node) override {
            TRACE_FUNCTION_F(&node << " { ... }");
            for (auto& n : node.m_nodes) {
                n->visit(*this);
            }
            if (node.m_value_node) {
                node.m_value_node->visit(*this);
                check_types_equal(node.span(), node.m_res_type, node.m_value_node->m_res_type);
            }
        }

        void visit(::HIR::ExprNodeConstBlock& node) override {
            TRACE_FUNCTION_F(&node << " const { ... }");
            node.m_inner->visit(*this);
            check_types_equal(node.span(), node.m_res_type, node.m_inner->m_res_type);
        }

        void visit(::HIR::ExprNodeAsm& node) override {
            TRACE_FUNCTION_F(&node << " llvm_asm! ...");

            // TODO: Check result types
            for (auto& v : node.m_outputs) {
                v.value->visit(*this);
            }
            for (auto& v : node.m_inputs) {
                v.value->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeAsm2& node) override {
            TRACE_FUNCTION_F(&node << " asm! ...");

            // TODO: Check result types
            for (auto& v : node.m_params) {
                TU_MATCH_HDRA( (v), { )
                TU_ARMA(Const, e) {
                        visit_node_ptr(e);
                    }
                    TU_ARMA(Sym, e) {
                    }
                    TU_ARMA(RegSingle, e) {
                        visit_node_ptr(e.val);
                    }
                    TU_ARMA(Reg, e) {
                        if (e.val_in) {
                            visit_node_ptr(e.val_in);
                        }
                        if (e.val_out) {
                            visit_node_ptr(e.val_out);
                        }
                    }
                }
            }
        }

        void visit(::HIR::ExprNodeReturn& node) override {
            TRACE_FUNCTION_F(&node << " return ...");
            // Check against return type
            const auto* ret_ty = (this->closure_ret_types.size() > 0 ? this->closure_ret_types.back().ret_type : this->ret_type);
            check_types_equal(ret_ty, node.m_value);
            node.m_value->visit(*this);
        }

        void visit(::HIR::ExprNodeYield& node) override {
            TRACE_FUNCTION_F(&node << " yield ...");
            ASSERT_BUG(node.span(), !this->closure_ret_types.empty(), "Yield outside a generator closure");
            ASSERT_BUG(node.span(), this->closure_ret_types.back().yield_type, "Yield outside a generator closure");
            check_types_equal(this->closure_ret_types.back().yield_type, node.m_value);
            node.m_value->visit(*this);
        }

        void visit(::HIR::ExprNodeAWait& node) override {
            node.m_value->visit(*this);
            auto t = m_resolve.m_crate.m_types.path(::HIR::Path(node.m_value->m_res_type, m_resolve.m_lang_Future, "Output"), {});
            m_resolve.expand_associated_types(node.span(), t);
            check_types_equal(node.span(), node.m_res_type, t);
        }

        void visit(::HIR::ExprNodeLoop& node) override {
            TRACE_FUNCTION_F(&node << " loop { ... }");
            m_loops.push_back(&node);
            node.m_code->visit(*this);
            m_loops.pop_back();
        }

        void visit(::HIR::ExprNodeLoopControl& node) override {
            TRACE_FUNCTION_F(&node << " " << (node.m_continue ? "continue" : "break") << " '" << node.m_label);

            if (node.m_value) {
                node.m_value->visit(*this);
            }

            if (!node.m_continue) {
                ::HIR::TypeRef unit = m_resolve.m_crate.m_types.unit();
                const auto& ty = (node.m_value ? node.m_value->m_res_type : unit);

                auto it = ::std::find(this->m_loops.rbegin(), this->m_loops.rend(), node.m_target_node);
                ASSERT_BUG(node.span(), it != this->m_loops.rend(), "Loop target node not found in the loop stack");

                DEBUG("Breaking to " << node.m_target_node << ", type " << node.m_target_node->m_res_type);
                check_types_equal(node.span(), node.m_target_node->m_res_type, ty);
            }
        }

        void visit(::HIR::ExprNodeLet& node) override {
            TRACE_FUNCTION_F(&node << " let " << node.m_pattern << ": " << node.m_type);
            if (node.m_value) {
                check_pattern(node.m_pattern, node.m_value->m_res_type);
                check_types_equal(node.span(), node.m_type, node.m_value->m_res_type);
                node.m_value->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeMatch& node) override {
            TRACE_FUNCTION_F(&node << " match ...");
            node.m_value->visit(*this);
            for (auto& arm : node.m_arms) {
                for (const auto& pat : arm.m_patterns) {
                    check_pattern(pat, node.m_value->m_res_type);
                }
                check_types_equal(node.span(), node.m_res_type, arm.m_code->m_res_type);
                arm.m_code->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeAssign& node) override {
            TRACE_FUNCTION_F(&node << "... ?= ...");

            if (node.m_op == ::HIR::ExprNodeAssign::Op::None) {
                check_types_equal(node.span(), node.m_slot->m_res_type, node.m_value->m_res_type);
            } else {
                // Type inferrence using the +=
                // - "" as type name to indicate that it's just using the trait magic?
                const char* lang_item = nullptr;
                auto operator_kind = typeck::PrimitiveOperator::None;
                switch (node.m_op) {
                    case ::HIR::ExprNodeAssign::Op::None:
                        throw "";
                    case ::HIR::ExprNodeAssign::Op::Add:
                        lang_item = "add_assign";
                        operator_kind = typeck::PrimitiveOperator::AddAssign;
                        break;
                    case ::HIR::ExprNodeAssign::Op::Sub:
                        lang_item = "sub_assign";
                        operator_kind = typeck::PrimitiveOperator::SubAssign;
                        break;
                    case ::HIR::ExprNodeAssign::Op::Mul:
                        lang_item = "mul_assign";
                        operator_kind = typeck::PrimitiveOperator::MulAssign;
                        break;
                    case ::HIR::ExprNodeAssign::Op::Div:
                        lang_item = "div_assign";
                        operator_kind = typeck::PrimitiveOperator::DivAssign;
                        break;
                    case ::HIR::ExprNodeAssign::Op::Mod:
                        lang_item = "rem_assign";
                        operator_kind = typeck::PrimitiveOperator::RemAssign;
                        break;
                    case ::HIR::ExprNodeAssign::Op::And:
                        lang_item = "bitand_assign";
                        operator_kind = typeck::PrimitiveOperator::BitAndAssign;
                        break;
                    case ::HIR::ExprNodeAssign::Op::Or:
                        lang_item = "bitor_assign";
                        operator_kind = typeck::PrimitiveOperator::BitOrAssign;
                        break;
                    case ::HIR::ExprNodeAssign::Op::Xor:
                        lang_item = "bitxor_assign";
                        operator_kind = typeck::PrimitiveOperator::BitXorAssign;
                        break;
                    case ::HIR::ExprNodeAssign::Op::Shr:
                        lang_item = "shr_assign";
                        operator_kind = typeck::PrimitiveOperator::ShrAssign;
                        break;
                    case ::HIR::ExprNodeAssign::Op::Shl:
                        lang_item = "shl_assign";
                        operator_kind = typeck::PrimitiveOperator::ShlAssign;
                        break;
                }
                assert(lang_item);
                if (!typeck::primitive_operator_has_builtin(operator_kind, node.m_slot->m_res_type, node.m_value->m_res_type)) {
                    const auto& trait_path = this->get_lang_item_path(node.span(), lang_item);
                    check_trait_bound(node.span(), trait_path, {node.m_value->m_res_type}, node.m_slot->m_res_type);
                }
            }

            node.m_slot->visit(*this);
            node.m_value->visit(*this);
        }

        void visit(::HIR::ExprNodeBinOp& node) override {
            TRACE_FUNCTION_F(&node << "... " << ::HIR::ExprNodeBinOp::opname(node.m_op) << " ...");

            switch (node.m_op) {
                case ::HIR::ExprNodeBinOp::Op::CmpEqu:
                case ::HIR::ExprNodeBinOp::Op::CmpNEqu:
                case ::HIR::ExprNodeBinOp::Op::CmpLt:
                case ::HIR::ExprNodeBinOp::Op::CmpLtE:
                case ::HIR::ExprNodeBinOp::Op::CmpGt:
                case ::HIR::ExprNodeBinOp::Op::CmpGtE: {
                    check_types_equal(node.span(), m_resolve.m_crate.m_types.primitive(::HIR::CoreType::Bool), node.m_res_type);

                    const char* item_name = nullptr;
                    switch (node.m_op) {
                        case ::HIR::ExprNodeBinOp::Op::CmpEqu:
                            item_name = "eq";
                            break;
                        case ::HIR::ExprNodeBinOp::Op::CmpNEqu:
                            item_name = "eq";
                            break;
                        case ::HIR::ExprNodeBinOp::Op::CmpLt:
                            item_name = "partial_ord";
                            break;
                        case ::HIR::ExprNodeBinOp::Op::CmpLtE:
                            item_name = "partial_ord";
                            break;
                        case ::HIR::ExprNodeBinOp::Op::CmpGt:
                            item_name = "partial_ord";
                            break;
                        case ::HIR::ExprNodeBinOp::Op::CmpGtE:
                            item_name = "partial_ord";
                            break;
                        default:
                            break;
                    }
                    assert(item_name);
                    auto operator_kind = node.m_op == ::HIR::ExprNodeBinOp::Op::CmpEqu || node.m_op == ::HIR::ExprNodeBinOp::Op::CmpNEqu
                        ? typeck::PrimitiveOperator::Equal
                        : typeck::PrimitiveOperator::Order;
                    if (!typeck::primitive_operator_has_builtin(operator_kind, node.m_left->m_res_type, node.m_right->m_res_type)) {
                        const auto& op_trait = this->get_lang_item_path(node.span(), item_name);
                        check_trait_bound(node.span(), op_trait, {node.m_right->m_res_type}, node.m_left->m_res_type);
                    }
                    break;
                }

                case ::HIR::ExprNodeBinOp::Op::BoolAnd:
                case ::HIR::ExprNodeBinOp::Op::BoolOr:
                    // No validation needed, result forced in typeck
                    break;
                default: {
                    const char* item_name = nullptr;
                    auto operator_kind = typeck::PrimitiveOperator::None;
                    switch (node.m_op) {
                        case ::HIR::ExprNodeBinOp::Op::CmpEqu:
                            throw "";
                        case ::HIR::ExprNodeBinOp::Op::CmpNEqu:
                            throw "";
                        case ::HIR::ExprNodeBinOp::Op::CmpLt:
                            throw "";
                        case ::HIR::ExprNodeBinOp::Op::CmpLtE:
                            throw "";
                        case ::HIR::ExprNodeBinOp::Op::CmpGt:
                            throw "";
                        case ::HIR::ExprNodeBinOp::Op::CmpGtE:
                            throw "";
                        case ::HIR::ExprNodeBinOp::Op::BoolAnd:
                            throw "";
                        case ::HIR::ExprNodeBinOp::Op::BoolOr:
                            throw "";

                        case ::HIR::ExprNodeBinOp::Op::Add:
                            item_name = "add";
                            operator_kind = typeck::PrimitiveOperator::Add;
                            break;
                        case ::HIR::ExprNodeBinOp::Op::Sub:
                            item_name = "sub";
                            operator_kind = typeck::PrimitiveOperator::Sub;
                            break;
                        case ::HIR::ExprNodeBinOp::Op::Mul:
                            item_name = "mul";
                            operator_kind = typeck::PrimitiveOperator::Mul;
                            break;
                        case ::HIR::ExprNodeBinOp::Op::Div:
                            item_name = "div";
                            operator_kind = typeck::PrimitiveOperator::Div;
                            break;
                        case ::HIR::ExprNodeBinOp::Op::Mod:
                            item_name = "rem";
                            operator_kind = typeck::PrimitiveOperator::Rem;
                            break;

                        case ::HIR::ExprNodeBinOp::Op::And:
                            item_name = "bitand";
                            operator_kind = typeck::PrimitiveOperator::BitAnd;
                            break;
                        case ::HIR::ExprNodeBinOp::Op::Or:
                            item_name = "bitor";
                            operator_kind = typeck::PrimitiveOperator::BitOr;
                            break;
                        case ::HIR::ExprNodeBinOp::Op::Xor:
                            item_name = "bitxor";
                            operator_kind = typeck::PrimitiveOperator::BitXor;
                            break;

                        case ::HIR::ExprNodeBinOp::Op::Shr:
                            item_name = "shr";
                            operator_kind = typeck::PrimitiveOperator::Shr;
                            break;
                        case ::HIR::ExprNodeBinOp::Op::Shl:
                            item_name = "shl";
                            operator_kind = typeck::PrimitiveOperator::Shl;
                            break;
                    }
                    assert(item_name);
                    if (!typeck::primitive_operator_has_builtin(operator_kind, node.m_left->m_res_type, node.m_right->m_res_type)) {
                        const auto& op_trait = this->get_lang_item_path(node.span(), item_name);
                        check_associated_type(node.span(), node.m_res_type, op_trait, {node.m_right->m_res_type}, node.m_left->m_res_type, "Output");
                    }
                    break;
                }
            }

            node.m_left->visit(*this);
            node.m_right->visit(*this);
        }

        void visit(::HIR::ExprNodeUniOp& node) override {
            TRACE_FUNCTION_F(&node << " " << ::HIR::ExprNodeUniOp::opname(node.m_op) << "...");
            auto operator_kind = typeck::PrimitiveOperator::None;
            switch (node.m_op) {
                case ::HIR::ExprNodeUniOp::Op::Invert:
                    operator_kind = typeck::PrimitiveOperator::Not;
                    if (!typeck::primitive_operator_has_builtin(operator_kind, node.m_value->m_res_type)) {
                        check_associated_type(node.span(), node.m_res_type, this->get_lang_item_path(node.span(), "not"), {}, node.m_value->m_res_type, "Output");
                    }
                    break;
                case ::HIR::ExprNodeUniOp::Op::Negate:
                    operator_kind = typeck::PrimitiveOperator::Neg;
                    if (!typeck::primitive_operator_has_builtin(operator_kind, node.m_value->m_res_type)) {
                        check_associated_type(node.span(), node.m_res_type, this->get_lang_item_path(node.span(), "neg"), {}, node.m_value->m_res_type, "Output");
                    }
                    break;
            }
            node.m_value->visit(*this);
        }

        void visit(::HIR::ExprNodeBorrow& node) override {
            TRACE_FUNCTION_F(&node << " &_ ...");
            check_types_equal(node.span(), node.m_res_type, m_resolve.m_crate.m_types.borrow(node.m_type, node.m_value->m_res_type));
            node.m_value->visit(*this);
        }

        void visit(::HIR::ExprNodeRawBorrow& node) override {
            TRACE_FUNCTION_F(&node << " &raw _ ...");
            check_types_equal(node.span(), node.m_res_type, m_resolve.m_crate.m_types.pointer(node.m_type, node.m_value->m_res_type));
            node.m_value->visit(*this);
        }

        void visit(::HIR::ExprNodeIndex& node) override {
            TRACE_FUNCTION_F(&node << " ... [ ... ]");
            check_associated_type(node.span(), node.m_res_type, m_lang_Index, {node.m_index->m_res_type}, node.m_value->m_res_type, "Output");

            node.m_value->visit(*this);
            node.m_index->visit(*this);
        }

        void visit(::HIR::ExprNodeCast& node) override {
            TRACE_FUNCTION_F(&node << " " << node.m_value->m_res_type << " as " << node.m_dst_type);
            const Span& sp = node.span();
            DEBUG("Cast res type " << node.m_res_type);
            //ASSERT_BUG(node.span(), node.m_res_type == node.m_dst_type, node.m_res_type << " != " << node.m_dst_type);

            const auto& src_ty = node.m_value->m_res_type;
            const auto& dst_ty = node.m_res_type;

            if (dst_ty == src_ty) {
                // Would be nice to delete it, but this is a readonly pass
                return;
            }

            // Check castability
            TU_MATCH_HDRA( ((*dst_ty)), {)
            default:
                ERROR(sp, E0000, "Invalid cast to\n " << dst_ty << "\n from\n " << src_ty);
                TU_ARMA(Pointer, de) {
                TU_MATCH_HDRA( ((*src_ty)), {)
                default:
                    ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty);
                        TU_ARMA(Pointer, se) {
                            // TODO: Sized check - can't cast to a fat pointer from a thin one
                            //if( ! this->m_resolve.type_is_sized(*de.inner) ) {
                            //    ERROR(sp, E0000, "Invalid cast to fat pointer " << dst_ty << " from " << src_ty);
                            //}
                        }
                        TU_ARMA(Primitive, se) {
                            switch (se) {
                                case ::HIR::CoreType::Bool:
                                case ::HIR::CoreType::Char:
                                case ::HIR::CoreType::Str:
                                case ::HIR::CoreType::F32:
                                case ::HIR::CoreType::F64:
                                    ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty);
                                default:
                                    break;
                            }
                            //if( ! this->m_resolve.type_is_sized(*de.inner) ) {
                            //    ERROR(sp, E0000, "Invalid cast to fat pointer " << dst_ty << " from " << src_ty);
                            //}
                        }
                        break;
                        case ::HIR::TypeData::TAG_Function:
                        case ::HIR::TypeData::TAG_NamedFunction:
                            if (de.inner == m_resolve.m_crate.m_types.unit() || de.inner == ::HIR::CoreType::U8 || de.inner == ::HIR::CoreType::I8) {
                            } else if (m_resolve.type_is_sized(sp, de.inner)) {
                                // Allow it.
                            } else {
                                ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty);
                            }
                            TU_ARMA(Borrow, se) {
                                this->check_types_equal(sp, de.inner, se.inner);
                            }
                }
                }
                TU_ARMA(Function, de) {
                    // NOTE: cast fn() only valid from:
                    // - the same function pointer (already checked, but eventually could be a stripping of the path tag)
                    // - A capture-less closure
                TU_MATCH_HDRA( ((*src_ty)), {)
                default:
                    ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty);
                        break;
                        TU_ARMA(NamedFunction, se) {
                            // TODO: Check?
                        }
                        TU_ARMA(Function, se) {
                            if (se.is_unsafe != de.is_unsafe && se.is_unsafe) {
                                ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty << " - removing unsafe");
                            }
                            if (se.m_abi != de.m_abi) {
                                ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty << " - different ABI");
                            }
                            if (se.m_rettype != de.m_rettype) {
                                ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty << " - return type different");
                            }
                            if (se.m_arg_types.size() != de.m_arg_types.size()) {
                                ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty << " - argument count different");
                            }
                            for (size_t i = 0; i < se.m_arg_types.size(); i++) {
                                if (se.m_arg_types[i] != de.m_arg_types[i]) {
                                    ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty << " - argument " << i << " different");
                                }
                            }
                        }
                        TU_ARMA(NodeType, se) {
                            if (se.is_Closure()) {
                                // Allowed, but won't exist after expansion
                                // TODO: Check argument types
                            } else {
                                ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty << " - not a function");
                            }
                        }
                }
                }
                TU_ARMA(Primitive, de) {
                    // TODO: Check cast to primitive
                }
            }

            node.m_value->visit( *this );
        }

        void visit(::HIR::ExprNodeUnsize& node) override {
            TRACE_FUNCTION_F(&node << " ... : " << node.m_res_type);
            const Span& sp = node.span();

            const auto& src_ty = node.m_value->m_res_type;
            const auto& dst_ty = node.m_res_type;

            if (src_ty->is_Diverge()) {
                // Perfectly valid. (! can become anything)
            } else if (src_ty == dst_ty) {
            } else if (src_ty->is_Borrow() && dst_ty->is_Borrow()) {
                const auto& se = src_ty->as_Borrow();
                const auto& de = dst_ty->as_Borrow();
                if (se.type != de.type) {
                    ERROR(sp, E0000, "Invalid unsizing operation to " << dst_ty << " from " << src_ty << " - Borrow class mismatch");
                }
                const auto& src_ty = se.inner;
                const auto& dst_ty = de.inner;

                const auto& lang_Unsize = m_resolve.m_crate.get_lang_item_path_opt("unsize");
                if (!lang_Unsize.components().empty()) {
                    // _ == < `src_ty` as Unsize< `dst_ty` >::""
                    check_trait_bound(sp, lang_Unsize, {dst_ty}, src_ty);
                } else if (!m_resolve.can_unsize(sp, dst_ty, src_ty)) {
                    ERROR(sp, E0000, "Invalid unsizing operation to " << dst_ty << " from " << src_ty);
                }
            } else if (src_ty->is_Borrow() || dst_ty->is_Borrow()) {
                ERROR(sp, E0000, "Invalid unsizing operation to " << dst_ty << " from " << src_ty);
            } else {
                const auto& lang_CoerceUnsized = this->get_lang_item_path(node.span(), "coerce_unsized");
                // _ == < `src_ty` as CoerceUnsized< `dst_ty` >::""
                check_trait_bound(sp, lang_CoerceUnsized, {dst_ty}, src_ty);
            }

            node.m_value->visit(*this);
        }

        void visit(::HIR::ExprNodeDeref& node) override {
            TRACE_FUNCTION_F(&node << " *...");
            const auto& ty = node.m_value->m_res_type;

            const bool builtin = node.m_trait_used == ::HIR::ExprNodeDeref::TraitUsed::Builtin
                || (node.m_trait_used == ::HIR::ExprNodeDeref::TraitUsed::Unknown
                    && typeck::primitive_operator_has_builtin(typeck::PrimitiveOperator::Deref, ty));
            if (builtin && ty->is_Pointer()) {
                check_types_equal(node.span(), node.m_res_type, ty->as_Pointer().inner);
            } else if (builtin && ty->is_Borrow()) {
                check_types_equal(node.span(), node.m_res_type, ty->as_Borrow().inner);
            } else {
                check_associated_type(node.span(), node.m_res_type, this->get_lang_item_path(node.span(), "deref"), {}, node.m_value->m_res_type, "Target");
            }

            node.m_value->visit(*this);
        }

        void visit(::HIR::ExprNodeEmplace& node) override {
            switch (node.m_type) {
                case ::HIR::ExprNodeEmplace::Type::Noop:
                    assert(!node.m_place);

                    check_types_equal(node.span(), node.m_res_type, node.m_value->m_res_type);
                    break;
                case ::HIR::ExprNodeEmplace::Type::Boxer:
                    // TODO: Check trait and associated type
                    break;
                case ::HIR::ExprNodeEmplace::Type::Placer:
                    // TODO: Check trait
                    break;
            }

            if (node.m_place) {
                this->visit_node_ptr(node.m_place);
            }
            this->visit_node_ptr(node.m_value);
        }

        void visit(::HIR::ExprNodeTupleVariant& node) override {
            TRACE_FUNCTION_F(&node << " " << node.m_path << "(...,) [" << (node.m_is_struct ? "struct" : "enum") << "]");
            const auto& sp = node.span();

            // - Create ivars in path, and set result type
            const auto& ty = node.m_res_type;

            const ::HIR::t_tuple_fields* fields_ptr = nullptr;
            ASSERT_BUG(sp, ty->is_Path(), "Result type of _TupleVariant isn't Path");
            TU_MATCH(::HIR::TypePathBinding, (ty->as_Path().binding), (e), (Unbound, BUG(sp, "Unbound type in _TupleVariant - " << ty);), (Opaque, BUG(sp, "Opaque type binding in _TupleVariant - " << ty);), (Enum, const auto& var_name = node.m_path.m_path.components().back(); const auto& enm = *e; size_t idx = enm.find_variant(var_name); const auto& var_ty = enm.m_data.as_Data()[idx].type; const auto& str = *var_ty->as_Path().binding.as_Struct(); ASSERT_BUG(sp, str.m_data.is_Tuple(), "Pointed variant of TupleVariant (" << node.m_path << ") isn't a Tuple"); fields_ptr = &str.m_data.as_Tuple();), (Union, BUG(sp, "Union in TupleVariant");), (ExternType, BUG(sp, "ExternType in TupleVariant");), (Struct, ASSERT_BUG(sp, e->m_data.is_Tuple(), "Pointed struct in TupleVariant (" << node.m_path << ") isn't a Tuple"); fields_ptr = &e->m_data.as_Tuple();))
            assert(fields_ptr);
            const ::HIR::t_tuple_fields& fields = *fields_ptr;
            ASSERT_BUG(sp, fields.size() == node.m_args.size(), "");

            // Bind fields with type params (coercable)
            // TODO: Remove use of m_arg_types (maybe assert that cache is correct?)
            for (unsigned int i = 0; i < node.m_args.size(); i++) {
                const auto& des_ty_r = fields[i].ent;
                const auto* des_ty = &des_ty_r;
                if (monomorphise_type_needed(des_ty_r)) {
                    assert(node.m_arg_types[i] != ::HIR::TypeRef());
                    des_ty = &node.m_arg_types[i];
                }

                check_types_equal(*des_ty, node.m_args[i]);
            }

            for (auto& val : node.m_args) {
                val->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeStructLiteral& node) override {
            TRACE_FUNCTION_F(&node << " " << node.m_real_path << "{...} [" << (node.m_is_struct ? "struct" : "enum") << "]");
            const auto& sp = node.span();
            if (node.m_base_value) {
                check_types_equal(node.m_base_value->span(), node.m_res_type, node.m_base_value->m_res_type);
            }
            const auto& ty_path = node.m_real_path;

            // - Create ivars in path, and set result type
            const auto& ty = node.m_res_type;
            ASSERT_BUG(sp, ty->is_Path(), "Result type of _StructLiteral isn't Path");

            const ::HIR::t_struct_fields* fields_ptr = nullptr;
            TU_MATCH_HDRA( (ty->as_Path().binding), {)
            TU_ARMA(Unbound, e) {
                }
                TU_ARMA(Opaque, e) {
                }
                TU_ARMA(Enum, e) {
                    const auto& var_name = ty_path.m_path.components().back();
                    const auto& enm = *e;
                    auto idx = enm.find_variant(var_name);
                    ASSERT_BUG(sp, idx != SIZE_MAX, "");
                    ASSERT_BUG(sp, enm.m_data.is_Data(), "");
                    const auto& var = enm.m_data.as_Data()[idx];

                    const auto& str = *var.type->as_Path().binding.as_Struct();
                    ASSERT_BUG(sp, var.is_struct, "Struct literal for enum on non-struct variant");
                    fields_ptr = &str.m_data.as_Named();
                }
                TU_ARMA(Union, e) {
                    fields_ptr = &e->m_variants;
                    ASSERT_BUG(node.span(), node.m_values.size() > 0, "Union with no values");
                    ASSERT_BUG(node.span(), node.m_values.size() == 1, "Union with multiple values");
                    ASSERT_BUG(node.span(), !node.m_base_value, "Union can't have a base value");
                }
                TU_ARMA(ExternType, e) {
                    BUG(sp, "ExternType in StructLiteral");
                }
                TU_ARMA(Struct, e) {
                    if (e->m_data.is_Unit()) {
                        ASSERT_BUG(node.span(), node.m_values.size() == 0, "Values provided for unit-like struct");
                        ASSERT_BUG(node.span(), !node.m_base_value, "Values provided for unit-like struct");
                        return;
                    }

                    ASSERT_BUG(node.span(), e->m_data.is_Named(), "StructLiteral not pointing to a braced struct, instead " << e->m_data.tag_str() << " - " << ty);
                    fields_ptr = &e->m_data.as_Named();
                }
            }
            ASSERT_BUG(node.span(), fields_ptr, "Didn't get field for path in _StructLiteral - " << ty);
            const ::HIR::t_struct_fields& fields = *fields_ptr;
            for(const auto& fld : fields) {
                DEBUG(fld.name << ": " << fld.ty);
            }

            auto ms = MonomorphStatePtr(m_resolve.m_crate.m_types, ty, &ty_path.m_params, nullptr);

            // Bind fields with type params (coercable)
            for( auto& val : node.m_values)
            {
                const auto& name = val.first;
                auto it = ::std::find_if(fields.begin(), fields.end(), [&](const HIR::StructField& v) -> bool {
                    return v.name == name;
                });
                assert(it != fields.end());
                const auto& des_ty_r = it->ty;
                auto& des_ty_cache = node.m_value_types[it - fields.begin()];
                const auto* des_ty = &des_ty_r;

                DEBUG(name << " : " << des_ty_r);
                if (monomorphise_type_needed(des_ty_r)) {
                    ASSERT_BUG(node.span(), des_ty_cache != ::HIR::TypeRef(), "Type " << des_ty_r << " needs monomorph, but isn't in cache: Field " << name);
                    des_ty_cache = ms.monomorph_type(node.span(), des_ty_r);
                    m_resolve.expand_associated_types(node.span(), des_ty_cache);
                    des_ty = &des_ty_cache;
                }
                DEBUG("." << name << " : " << *des_ty);
                check_types_equal(*des_ty, val.second);
            }

            for( auto& val : node.m_values ) {
                val.second->visit(*this);
            }
            if( node.m_base_value ) {
                node.m_base_value->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeUnitVariant& node) override {
            TRACE_FUNCTION_F(&node << " " << node.m_path << " [" << (node.m_is_struct ? "struct" : "enum") << "]");
            const auto& sp = node.span();
            const auto& ty = node.m_res_type;
            ASSERT_BUG(sp, ty->is_Path(), "Result type of _UnitVariant isn't Path");

            TU_MATCH(
                ::HIR::TypePathBinding,
                (ty->as_Path().binding),
                (e),
                (Unbound, ),
                (Opaque, ),
                (
                    Enum, const auto& var_name = node.m_path.m_path.components().back(); const auto& enm = *e; if (const auto* e = enm.m_data.opt_Data()) {
                        auto idx = enm.find_variant(var_name);
                        ASSERT_BUG(sp, idx != SIZE_MAX, "");
                        ASSERT_BUG(sp, (*e)[idx].type == m_resolve.m_crate.m_types.unit(), "");
                    }
                ),
                (Union, BUG(sp, "Union with _UnitVariant");),
                (ExternType, BUG(sp, "ExternType with _UnitVariant");),
                (Struct, assert(e->m_data.is_Unit());)
            )
        }

        void check_function(const Span& sp, const ::HIR::Path& path, HIR::ExprCallCache& cache) {
            // Do function resolution again, this time with concrete types.
            const ::HIR::Function* fcn_ptr = nullptr;
            MonomorphStatePtr monomorph_cb(m_resolve.m_crate.m_types);

            TU_MATCH_HDRA( (path.m_data), {)
            TU_ARMA(Generic, e) {
                    const auto& path_params = e.m_params;

                    const auto& fcn = m_resolve.m_crate.get_function_by_path(sp, e.m_path);
                    fcn_ptr = &fcn;
                    cache.m_fcn_params = &fcn.m_params;

                    monomorph_cb = MonomorphStatePtr(m_resolve.m_crate.m_types, nullptr, nullptr, &path_params);
                }
                TU_ARMA(UfcsKnown, e) {
                    const auto& trait_params = e.trait.m_params;
                    const auto& path_params = e.params;

                    const auto& trait = m_resolve.m_crate.get_trait_by_path(sp, e.trait.m_path);
                    if (trait.m_values.count(e.item) == 0) {
                        BUG(sp, "Method '" << e.item << "' of trait " << e.trait.m_path << " doesn't exist");
                    }

                    const auto& fcn = trait.m_values.at(e.item).as_Function();
                    cache.m_fcn_params = &fcn.m_params;
                    cache.m_top_params = &trait.m_params;

                    // Add a bound requiring the Self type impl the trait
                    check_trait_bound(sp, e.trait.m_path, e.trait.m_params, e.type);

                    fcn_ptr = &fcn;

                    monomorph_cb = MonomorphStatePtr(m_resolve.m_crate.m_types, e.type, &trait_params, &path_params);
                }
                TU_ARMA(UfcsUnknown, e) {
                    TODO(sp, "Hit a UfcsUnknown (" << path << ") - Is this an error?");
                }
                TU_ARMA(UfcsInherent, e) {
                    // - Locate function (and impl block)
                    const ::HIR::TypeImpl* impl_ptr = nullptr;
                    m_resolve.m_crate.find_type_impls(e.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                        DEBUG("- impl" << impl.m_params.fmt_args() << " " << impl.m_type);
                        auto it = impl.m_methods.find(e.item);
                        if (it == impl.m_methods.end()) {
                            return false;
                        }
                        fcn_ptr = &it->second.data;
                        impl_ptr = &impl;
                        return true;
                    });
                    if (!fcn_ptr) {
                        ERROR(sp, E0000, "Failed to locate function " << path);
                    }
                    assert(impl_ptr);

                    cache.m_fcn_params = &fcn_ptr->m_params;

                    // NOTE: Trusts the existing cache.
                    ASSERT_BUG(sp, e.impl_params.m_types.size() == impl_ptr->m_params.m_types.size(), "Path impl_params cache is missized - " << e.impl_params.m_types.size() << " != " << impl_ptr->m_params.m_types.size());
                    auto& impl_params = e.impl_params;

                    // Create monomorphise callback
                    const auto& fcn_params = e.params;
                    monomorph_cb = MonomorphStatePtr(m_resolve.m_crate.m_types, e.type, &impl_params, &fcn_params);
                }
            }

            assert( fcn_ptr );
            const auto& fcn = *fcn_ptr;
            monomorph_cb.set_consteval_state(m_resolve.m_crate, HIR::ItemPath(path));

            // --- Monomorphise the argument/return types (into current context)
            cache.m_arg_types.clear();
            for(const auto& arg : fcn.m_args) {
                DEBUG("Arg " << arg.first << ": " << arg.second);
                cache.m_arg_types.push_back(monomorph_cb.monomorph_type(sp, arg.second, false));
                m_resolve.expand_associated_types(sp, cache.m_arg_types.back());
                DEBUG("= " << cache.m_arg_types.back());
            }
            DEBUG("Ret " << fcn.m_return);
            // Replace ErasedType and monomorphise
            cache.m_arg_types.push_back( monomorph_cb.monomorph_type(sp, fcn.m_return, false) );
            rewrite_ty_with(m_resolve.m_crate.m_types, cache.m_arg_types.back(), [&](HIR::TypeRef& ty, HIR::TypeData&)->bool {
                if (this->expand_erased_types && ty->is_ErasedType() && ty->as_ErasedType().m_inner.is_Fcn()) {
                    const auto& e = ty->as_ErasedType().m_inner.as_Fcn();

                    // Check the origin, because monomorph might end up introducing other erased types
                    if (e.m_origin == path) {
                        ASSERT_BUG(sp, e.m_index < fcn_ptr->m_code.m_erased_types.size(), "");
                        const auto& erased_type_replacement = fcn_ptr->m_code.m_erased_types.at(e.m_index);
                        ty = monomorph_cb.monomorph_type(sp, erased_type_replacement, false);
                        return true;
                    }
                }
                return false;
                });
            m_resolve.expand_associated_types(sp, cache.m_arg_types.back());
            DEBUG("= " << cache.m_arg_types.back());

            cache.m_monomorph.reset( new MonomorphStatePtr(monomorph_cb) );

            // Bounds
            for(size_t i = 0; i < cache.m_fcn_params->m_types.size(); i ++)
            {
            }
            for(const auto& bound : cache.m_fcn_params->m_bounds)
            {
                TU_MATCH_HDRA( (bound), {)
                TU_ARMA(Lifetime, be) {
                    }
                    TU_ARMA(TypeLifetime, be) {
                    }
                    TU_ARMA(TraitBound, be) {
                        HIR::GenericParams empty_hrtb;
                        auto _ = cache.m_monomorph->push_hrb(be.hrtbs ? *be.hrtbs : empty_hrtb);
                        DEBUG("Bound " << be.type << ":  " << be.trait);
                        auto real_type = cache.m_monomorph->monomorph_type(sp, be.type);
                        m_resolve.expand_associated_types(sp, real_type);
                        auto real_trait = cache.m_monomorph->monomorph_traitpath(sp, be.trait, false);
                        m_resolve.expand_associated_types_tp(sp, real_trait);
                        DEBUG("= (" << real_type << ": " << real_trait << ")");
                        const auto& trait_params = real_trait.m_path.m_params;

                        const auto& trait_path = be.trait.m_path.m_path;
                        check_trait_bound(sp, trait_path, trait_params, real_type);

                        // TODO: Either - Don't include the above impl bound, or change the below trait to the one that has that type
                        for (auto& assoc : real_trait.m_type_bounds) {
                            ::HIR::GenericPath type_trait_path;
                            bool has_ty = m_resolve.trait_contains_type(sp, real_trait.m_path, *be.trait.m_trait_ptr, assoc.first.c_str(), type_trait_path);
                            ASSERT_BUG(sp, has_ty, "Type " << assoc.first << " not found in chain of " << real_trait.m_path);

                            check_associated_type(sp, assoc.second.type, type_trait_path.m_path, type_trait_path.m_params, real_type, assoc.first.c_str());
                        }
                    }
                    TU_ARMA(TypeEquality, be) {
                        auto real_type_left = cache.m_monomorph->monomorph_type(sp, be.type);
                        auto real_type_right = cache.m_monomorph->monomorph_type(sp, be.other_type);
                        m_resolve.expand_associated_types(sp, real_type_left);
                        m_resolve.expand_associated_types(sp, real_type_right);
                        check_types_equal(sp, real_type_left, real_type_right);
                    }
                }
            }
        }

        void visit(::HIR::ExprNodeCallPath& node) override {
            const auto& sp = node.span();
            TRACE_FUNCTION_F(&node << " " << node.m_path << "(..., )");

            for (auto& val : node.m_args) {
                val->visit(*this);
            }

            check_function(sp, node.m_path, node.m_cache);

            // Check types
            for (unsigned int i = 0; i < node.m_cache.m_arg_types.size() - 1; i++) {
                DEBUG("CHECK ARG " << i << " " << node.m_cache.m_arg_types[i] << " == " << node.m_args[i]->m_res_type);
                check_types_equal(sp, node.m_cache.m_arg_types[i], node.m_args[i]->m_res_type);
            }
            for (unsigned int i = node.m_cache.m_arg_types.size() - 1; i < node.m_args.size(); i++) {
                DEBUG("CHECK ARG " << i << " *  == " << node.m_args[i]->m_res_type);
                // TODO: Check that the types here are valid.
            }
            DEBUG("CHECK RV " << node.m_res_type << " == " << node.m_cache.m_arg_types.back());
            check_types_equal(sp, node.m_res_type, node.m_cache.m_arg_types.back());
        }

        void visit(::HIR::ExprNodeCallValue& node) override {
            TRACE_FUNCTION_F(&node << " (...)(..., )");

            const auto& val_ty = node.m_value->m_res_type;

            if (val_ty->is_Function() || val_ty->is_NamedFunction()) {
                DEBUG("- Function pointer: " << val_ty);
                ::HIR::TypeRef tmp_ft;
                const auto* e = val_ty->opt_Function();
                if (!e) {
                    tmp_ft = m_resolve.m_crate.m_types.function(val_ty->as_NamedFunction().decay(m_resolve.m_crate.m_types, node.span()));
                    m_resolve.expand_associated_types(node.span(), tmp_ft);
                    e = &tmp_ft->as_Function();
                }
                auto hrls = e->hrls.make_empty_params(true);
                auto m = MonomorphHrlsOnly(m_resolve.m_crate.m_types, hrls);
                if (e->is_variadic ? node.m_args.size() < e->m_arg_types.size() : node.m_args.size() != e->m_arg_types.size()) {
                    ERROR(node.span(), E0000, "Incorrect number of arguments to call via " << val_ty);
                }
                for (unsigned int i = 0; i < e->m_arg_types.size(); i++) {
                    check_types_equal(node.m_args[i]->span(), m.monomorph_type(node.span(), e->m_arg_types[i]), node.m_args[i]->m_res_type);
                }
                check_types_equal(node.span(), node.m_res_type, m.monomorph_type(node.span(), e->m_rettype));
            } else if (node.m_trait_used == ::HIR::ExprNodeCallValue::TraitUsed::Unknown) {
            } else {
                // 1. Look up the encoded trait
                const ::HIR::SimplePath* trait_p;
                switch (node.m_trait_used) {
                    case ::HIR::ExprNodeCallValue::TraitUsed::Fn:
                        trait_p = &m_resolve.m_crate.get_lang_item_path(node.span(), "fn");
                        break;
                    case ::HIR::ExprNodeCallValue::TraitUsed::FnMut:
                        trait_p = &m_resolve.m_crate.get_lang_item_path(node.span(), "fn_mut");
                        break;
                    case ::HIR::ExprNodeCallValue::TraitUsed::FnOnce:
                        trait_p = &m_resolve.m_crate.get_lang_item_path(node.span(), "fn_once");
                        break;
                    default:
                        throw "";
                }
                const auto& trait = *trait_p;

                ::std::vector<::HIR::TypeRef> tup_ents;
                for (const auto& arg : node.m_args) {
                    tup_ents.push_back(arg->m_res_type);
                }
                ::HIR::PathParams params;
                params.m_types.push_back(m_resolve.m_crate.m_types.tuple(mv$(tup_ents)));

                bool found = m_resolve.find_impl(node.span(), trait, &params, val_ty, [&](auto, bool fuzzy) -> bool {
                    ASSERT_BUG(node.span(), !fuzzy, "Fuzzy match in check pass");
                    return true;
                });
                if (!found) {
                    ERROR(node.span(), E0000, "Unable to find a matching impl of " << trait << " for " << val_ty);
                }
                auto exp_ret = m_resolve.m_crate.m_types.path(::HIR::Path(node.m_value->m_res_type, {m_resolve.m_crate.get_lang_item_path(node.span(), "fn_once"), mv$(params)}, "Output", {}), {});
                m_resolve.expand_associated_types(node.span(), exp_ret);
                check_types_equal(node.span(), node.m_res_type, exp_ret);
            }

            node.m_value->visit(*this);
            for (auto& val : node.m_args) {
                val->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeCallMethod& node) override {
            TRACE_FUNCTION_F(&node << " (...)." << node.m_method << "(...,) - " << node.m_method_path);

            node.m_value->visit(*this);
            for (auto& val : node.m_args) {
                val->visit(*this);
            }

            const Span& sp = node.span();
            check_function(sp, node.m_method_path, node.m_cache);

            // Check types
            for (unsigned int i = 0; i < node.m_cache.m_arg_types.size() - 2; i++) {
                DEBUG("CHECK ARG " << i << " " << node.m_cache.m_arg_types[1 + i] << " == " << node.m_args[i]->m_res_type);
                check_types_equal(sp, node.m_cache.m_arg_types[1 + i], node.m_args[i]->m_res_type);
            }
            for (unsigned int i = node.m_cache.m_arg_types.size() - 1; i < node.m_args.size(); i++) {
                DEBUG("CHECK ARG " << i << " *  == " << node.m_args[i]->m_res_type);
                // TODO: Check that the types here are valid.
            }
            DEBUG("CHECK RV " << node.m_res_type << " == " << node.m_cache.m_arg_types.back());
            check_types_equal(sp, node.m_res_type, node.m_cache.m_arg_types.back());
        }

        void visit(::HIR::ExprNodeField& node) override {
            TRACE_FUNCTION_F(&node << " (...)." << node.m_field);
            const auto& sp = node.span();
            const auto& str_ty = node.m_value->m_res_type;

            bool is_index = ('0' <= node.m_field.c_str()[0] && node.m_field.c_str()[0] <= '9');
            if (str_ty->is_Tuple()) {
                ASSERT_BUG(sp, is_index, "Non-index _Field on tuple");
            } else if (str_ty->is_NodeType()) {
                ASSERT_BUG(sp, is_index, "Non-index _Field on magic type");
            } else {
                ASSERT_BUG(sp, str_ty->is_Path(), "Value type of _Field isn't Path - " << str_ty);
                const auto& ty_e = str_ty->as_Path();
                if (ty_e.binding.is_Struct()) {
                    //const auto& str = *ty_e.binding.as_Struct();
                    // TODO: Triple-check result, but that probably isn't needed
                } else if (ty_e.binding.is_Union()) {
                } else {
                    ASSERT_BUG(sp, ty_e.binding.is_Struct() || ty_e.binding.is_Union(), "Value type of _Field isn't a Struct or Union - " << str_ty);
                }
            }

            node.m_value->visit(*this);
        }

        void visit(::HIR::ExprNodeTuple& node) override {
            TRACE_FUNCTION_F(&node << " (...,)");
            ASSERT_BUG(node.span(), node.m_res_type->is_Tuple(), "Tuple literal didn't return tuple");
            const auto& tys = node.m_res_type->as_Tuple();

            ASSERT_BUG(node.span(), tys.size() == node.m_vals.size(), "Bad element count in tuple literal - " << tys.size() << " != " << node.m_vals.size());
            for (unsigned int i = 0; i < node.m_vals.size(); i++) {
                check_types_equal(node.span(), tys[i], node.m_vals[i]->m_res_type);
            }

            for (auto& val : node.m_vals) {
                val->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeArrayList& node) override {
            TRACE_FUNCTION_F(&node << " [...,]");
            // Cleanly equate into array (with coercions)
            const auto& inner_ty = node.m_res_type->as_Array().inner;
            for (auto& val : node.m_vals) {
                check_types_equal(val->span(), inner_ty, val->m_res_type);
            }

            for (auto& val : node.m_vals) {
                val->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeArraySized& node) override {
            TRACE_FUNCTION_F(&node << " [...; " << node.m_size << "]");

            //check_types_equal(node.m_size->span(), ::HIR::TypeRef(::HIR::Primitive::Usize), node.m_size->m_res_type);
            const auto& inner_ty = node.m_res_type->as_Array().inner;
            check_types_equal(node.m_val->span(), inner_ty, node.m_val->m_res_type);

            node.m_val->visit(*this);
            //if(node.m_size.is_Unevaluated() && node.m_size.as_Unevaluated().is_Unevaluated())
            //{
            //    (*node.m_size.as_Unevaluated().as_Unevaluated())->visit( *this );
            //}
        }

        void visit(::HIR::ExprNodeLiteral& node) override {
            // No validation needed
        }

        void visit(::HIR::ExprNodePathValue& node) override {
            TRACE_FUNCTION_F(&node << " " << node.m_path);
            const Span& sp = node.span();

            MonomorphState out_params(m_resolve.m_crate.m_types);
            StaticTraitResolve::ValuePtr v = this->m_resolve.get_value(sp, node.m_path, out_params, /*signature_only=*/true);
            HIR::TypeRef ty;
            TU_MATCH_HDRA( (v), {)
            TU_ARMA(NotFound, ve) {
                    BUG(sp, node.m_path << " Not found");
                }
                TU_ARMA(NotYetKnown, ve) {
                    // If the exact value can't be found, then
                    BUG(sp, node.m_path << " still unknown (has ivars?)");
                }
                TU_ARMA(Static, ve) {
                    ty = out_params.monomorph_type(node.span(), ve->m_type);
                    this->m_resolve.expand_associated_types(sp, ty);
                }
                TU_ARMA(Constant, ve) {
                    ty = out_params.monomorph_type(node.span(), ve->m_type);
                    this->m_resolve.expand_associated_types(sp, ty);
                }
                TU_ARMA(StructConstant, ve) {
                    // TODO: Check struct type
                }
                TU_ARMA(EnumValue, ve) {
                    // TODO: Check enum variant type
                }

                TU_ARMA(Function, ve) {
                    ty = m_resolve.m_crate.m_types.intern(::HIR::TypeData::make_NamedFunction({node.m_path.clone(), ve}));
                }
                TU_ARMA(StructConstructor, ve) {
                    ty = m_resolve.m_crate.m_types.intern(::HIR::TypeData::make_NamedFunction({node.m_path.clone(), ve.s}));
                }
                TU_ARMA(EnumConstructor, ve) {
                    ty = m_resolve.m_crate.m_types.intern(::HIR::TypeData::make_NamedFunction({node.m_path.clone(), ::HIR::TypeDataNamedFunctionTy::make_EnumConstructor({ve.e, ve.v})}));
                }
            }
            if( ty != HIR::TypeRef() ) {
                check_types_equal(sp, node.m_res_type, ty);
            }
        }

        void visit(::HIR::ExprNodeVariable& node) override {
            // TODO: Check against variable slot? Nah.
        }

        void visit(::HIR::ExprNodeConstParam& node) override {
            // TODO: Check against variable slot? Nah.
        }

        void visit(::HIR::ExprNodeClosure& node) override {
            TRACE_FUNCTION_F(&node << " |...| ...");

            if (node.m_code) {
                check_types_equal(node.m_code->span(), node.m_return, node.m_code->m_res_type);

                auto loops = ::std::move(this->m_loops);

                this->closure_ret_types.push_back(RetTarget(node.m_return));
                node.m_code->visit(*this);
                this->closure_ret_types.pop_back();

                this->m_loops = ::std::move(loops);
            }
        }

        void visit(::HIR::ExprNodeGenerator& node) override {
            TRACE_FUNCTION_F(&node << " /*gen*/ |...| ...");

            if (node.m_code) {
                auto loops = ::std::move(this->m_loops);

                check_types_equal(node.m_code->span(), node.m_return, node.m_code->m_res_type);
                this->closure_ret_types.push_back(RetTarget(node.m_return, node.m_yield_ty));
                node.m_code->visit(*this);
                this->closure_ret_types.pop_back();

                this->m_loops = ::std::move(loops);
            }
        }

        void visit(::HIR::ExprNodeGeneratorWrapper& node) override {
            TRACE_FUNCTION_F(&node << " /*gen w*/ |...| ...");

            if (node.m_code) {
                auto loops = ::std::move(this->m_loops);

                check_types_equal(node.m_code->span(), node.m_return, node.m_code->m_res_type);
                this->closure_ret_types.push_back(RetTarget(node.m_return, node.m_yield_ty));
                node.m_code->visit(*this);
                this->closure_ret_types.pop_back();

                this->m_loops = ::std::move(loops);
            }
        }

        void visit(::HIR::ExprNodeAsyncBlock& node) override {
            TRACE_FUNCTION_F(&node << " async { ... }");

            // Can be null after generation
            if (node.m_code) {
                auto loops = ::std::move(this->m_loops);
                this->closure_ret_types.push_back(RetTarget(node.m_code->m_res_type));
                node.m_code->visit(*this);
                this->closure_ret_types.pop_back();
                this->m_loops = ::std::move(loops);
            }
        }

    private:
        void check_types_equal(const ::HIR::TypeData* l, const ::HIR::ExprNodeP& node) const {
            check_types_equal(node->span(), l, node->m_res_type);
        }

        void check_types_equal(const Span& sp, const ::HIR::TypeData* l, const ::HIR::TypeData* r) const {
            struct Resolve: HIR::ResolvePlaceholders {
                HIR::TypeInterner& types;
                mutable ::HIR::TypeRef tmp;

                explicit Resolve(HIR::TypeInterner& types): types(types) {}

                const ::HIR::TypeData* get_type(const Span& sp, const HIR::TypeData* ty) const override {
                    //ASSERT_BUG(sp, ty->is_Infer(), "Unexpected ivar");
                    if (const auto* e = ty->opt_ErasedType()) {
                        if (const auto* ee = e->m_inner.opt_Alias()) {
                            if (ee->inner->type != HIR::TypeRef()) {
                                return tmp = MonomorphStatePtr(types, nullptr, &ee->params, nullptr).monomorph_type(sp, ee->inner->type);
                            }
                        }
                    }
                    return ty;
                }

                const ::HIR::ConstGeneric& get_val(const Span& sp, const HIR::ConstGeneric& v) const override {
                    return v;
                }
            } get_types(m_resolve.m_crate.m_types);

            // TODO: Recurse when an erased type is encountered
            //if( const auto* e = l->opt_ErasedType() )
            //{
            //    return check_types_equal(sp, m_cur_expr->m_erased_types.at(e->m_index), r);
            //}
            //if( const auto* e = r->opt_ErasedType() )
            //{
            //    return check_types_equal(sp, l, m_cur_expr->m_erased_types.at(e->m_index));
            //}
            DEBUG(sp << " - " << l << " == " << r);
            MonomorphHrlsOnly(m_resolve.m_crate.m_types, HIR::PathParams()).monomorph_type(sp, l);
            MonomorphHrlsOnly(m_resolve.m_crate.m_types, HIR::PathParams()).monomorph_type(sp, r);
            if (/*l->is_Diverge() ||*/ r->is_Diverge()) {
                // Diverge, matches everything.
                // TODO: Is this always true?
            } else if (l->compare_with_placeholders(sp, r, get_types) != HIR::Compare::Equal) {
                ERROR(sp, E0000, "Type mismatch\n - " << l << "\n!= " << r);
            } else {
                // All good
            }
        }

        void check_trait_bound(
            const Span& sp,
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* ity
        ) const {
            DEBUG(sp << " - " << ity << " : " << trait << params);
            auto normalized_type = ity;
            auto normalized_params = params.clone();
            m_resolve.expand_associated_types(sp, normalized_type);
            for (auto& type : normalized_params.m_types) {
                m_resolve.expand_associated_types(sp, type);
            }
            const bool found = m_resolve.find_impl(
                sp,
                trait,
                &normalized_params,
                normalized_type,
                [](auto, bool) {
                return true;
                }
            );
            if (!found) {
                ERROR(sp, E0000, "Cannot find an impl of " << trait << normalized_params << " for " << normalized_type);
            }
        }

        void check_associated_type(
            const Span& sp,
            const ::HIR::TypeData* res, // Expected result
            const ::HIR::SimplePath& trait,
            const ::HIR::PathParams& params,
            const ::HIR::TypeData* ity,
            const char* name
            // TODO: Does this need params for the ATY??
        ) const {
            ASSERT_BUG(sp, name && name[0], "check_associated_type called without an associated type name");
            DEBUG(sp << " - " << res << " == < " << ity << " as " << trait << params << " >::" << name);
            bool found = m_resolve.find_impl(sp, trait, &params, ity, [&](auto impl, bool fuzzy) {
                auto atyv = impl.get_type(m_resolve.m_crate.m_types, name, {});
                if (atyv == ::HIR::TypeRef()) {
                    // TODO: Check that `res` is <ity as trait>::name
                } else {
                    m_resolve.expand_associated_types(sp, atyv);
                    if (res != atyv && !res->equals_ignoring_regions(atyv)) {
                        ERROR(sp, E0000, "Associated type on " << trait << params << " for " << ity << " doesn't match - " << res << " != " << atyv);
                    }
                }

                return true;
            });
            if (!found) {
                ERROR(sp, E0000, "Cannot find an impl of " << trait << params << " for " << ity);
            }
        }

        void check_pattern(const ::HIR::Pattern& pat, const ::HIR::TypeData* top_ty) const {
            Span sp;
            TRACE_FUNCTION_F("pat=" << pat << " ty=" << top_ty);
            const ::HIR::TypeData* typ = top_ty;
            // Implicit derefs
            for (size_t i = 0; i < pat.m_implicit_deref_count; i++) {
                typ = typ->as_Borrow().inner;
            }
            const ::HIR::TypeData* ty = typ;

            TU_MATCH_HDRA( (pat.m_data), { )
            TU_ARMA(Any, pe) {
                    // Don't care
                }
                TU_ARMA(Box, pe) {
                    // TODO: Assert that `ty` is an owned_box
                }
                TU_ARMA(Ref, pe) {
                    // TODO: Assert that `ty` is a &-ptr
                }
                TU_ARMA(Tuple, pe) {
                    // TODO: Check for a matching tuple size
                }
                TU_ARMA(SplitTuple, pe) {
                    // TODO: Check for a matching tuple size
                }
                TU_ARMA(PathValue, pe) {
                    // TODO: Check that the type matches the struct
                }
                TU_ARMA(PathTuple, pe) {
                    // TODO: Destructure
                }
                TU_ARMA(PathNamed, pe) {
                    // TODO: Destructure
                }

                TU_ARMA(Value, pe) {
                    this->check_pattern_value(sp, pe.val, ty);
                }
                TU_ARMA(Range, pe) {
                    if (pe.start) {
                        this->check_pattern_value(sp, *pe.start, ty);
                    }
                    if (pe.end) {
                        this->check_pattern_value(sp, *pe.end, ty);
                    }
                }
                TU_ARMA(Slice, e) {
                    // TODO: Check that the type is a Slice or Array
                    // - Array must match size
                }
                TU_ARMA(SplitSlice, e) {
                    // TODO: Check that the type is a Slice or Array
                    // - Array must have compatible size
                }

                TU_ARMA(Or, e) {
                    for (auto& subpat : e) {
                        check_pattern(subpat, ty);
                    }
                }
            }
        }

        void check_pattern_value(const Span& sp, const ::HIR::Pattern::Value& pv, const ::HIR::TypeData* ty) const {
            TU_MATCH_HDRA( (pv), { )
            TU_ARMA(Integer, e) {
                    if (e.type == ::HIR::CoreType::Str) {
                    } else {
                        check_types_equal(sp, ty, m_resolve.m_crate.m_types.primitive(e.type));
                    }
                }
                TU_ARMA(Float, e) {
                    if (e.type == ::HIR::CoreType::Str) {
                    } else {
                        check_types_equal(sp, ty, m_resolve.m_crate.m_types.primitive(e.type));
                    }
                }
                TU_ARMA(String, e) {
                    check_types_equal(sp, ty, m_resolve.m_crate.m_types.borrow(::HIR::BorrowType::Shared, m_resolve.m_crate.m_types.primitive(::HIR::CoreType::Str)));
                }
                TU_ARMA(ByteString, e) {
                    // Can either be a slice or an array
                    //check_types_equal(sp, ty, ::HIR::TypeRef::new_borrow(::HIR::BorrowType::Shared, ::HIR::TypeRef::new_slice(::HIR::CoreType::U8)));
                }
                TU_ARMA(Named, e) {
                    MonomorphState ms(m_resolve.m_crate.m_types);
                    auto v = m_resolve.get_value(sp, e.path, ms, /*signature_only*/ true);
                    if (!v.is_Constant()) {
                        BUG(sp, "Pattern::Value::Named not a const - " << e.path);
                    }
                    HIR::TypeRef tmp;
                    const auto& const_ty = ms.maybe_monomorph_type(sp, tmp, v.as_Constant()->m_type);
                    check_types_equal(sp, ty, const_ty);
                }
            }
        }

        const ::HIR::SimplePath& get_lang_item_path(const Span& sp, const char* name) const {
            return m_resolve.m_crate.get_lang_item_path(sp, name);
        }
    };

    class OuterVisitor: public ::HIR::Visitor {
        StaticTraitResolve m_resolve;

    public:
        OuterVisitor(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.m_types)
            , m_resolve(crate)
        {
        }

        // NOTE: This is left here to ensure that any expressions that aren't handled by higher code cause a failure
        void visit_expr(::HIR::ExprPtr& exp) override {
            BUG(Span(), "visit_expr hit in OuterVisitor");
        }

        void visit_type(::HIR::TypeRef& ty) override {
            if (ty->is_Array()) {
                auto data = ty->clone_data();
                auto& e = data.as_Array();
                this->visit_type(e.inner);
                DEBUG("Array size " << ty);
                if (auto* se1 = e.size.opt_Unevaluated()) {
                    if (auto* se = se1->opt_Unevaluated()) {
                        t_args tmp;
                        auto ty_usize = m_resolve.m_crate.m_types.primitive(::HIR::CoreType::Usize);
                        ExprVisitorValidate ev(m_resolve, tmp, ty_usize);
                        ev.visit_root(*(*se)->expr);
                    }
                }
                ty = m_resolve.m_crate.m_types.intern(std::move(data));
            } else {
                ::HIR::Visitor::visit_type(ty);
            }
        }

        void visit_constgeneric(::HIR::ConstGeneric& value) override {
            if (auto* unevaluated = value.opt_Unevaluated()) {
                t_args tmp;
                auto& expr = *(**unevaluated).expr;
                ExprVisitorValidate ev(m_resolve, tmp, expr->m_res_type);
                ev.visit_root(expr);
            }
        }

        // ------
        // Code-containing items
        // ------
        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            auto _ = this->m_resolve.set_item_generics(item.m_params);
            if (item.m_code) {
                DEBUG("Function code " << p);
                ::HIR::TypeRef tmp;
                const auto& ret_ty = m_resolve.fix_trait_default_return(item.m_code->span(), p, item.m_return, tmp);
                ExprVisitorValidate ev(m_resolve, item.m_args, ret_ty);
                ev.visit_root(item.m_code);
            } else {
                DEBUG("Function code " << p << " (none)");
            }
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            auto _ = this->m_resolve.set_item_generics(item.m_params);
            if (item.m_value) {
                t_args tmp;
                ExprVisitorValidate ev(m_resolve, tmp, item.m_type);
                ev.visit_root(item.m_value);
            }
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            auto _ = this->m_resolve.set_item_generics(item.m_params);
            if (item.m_value) {
                t_args tmp;
                ExprVisitorValidate ev(m_resolve, tmp, item.m_type);
                ev.visit_root(item.m_value);
            }
            m_resolve.expand_associated_types(Span(), item.m_type);
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = this->m_resolve.set_impl_generics(MetadataType::None, item.m_params);

            ::HIR::TypeRef enum_type = m_resolve.m_crate.m_types.primitive(::HIR::Enum::get_repr_type(item.m_tag_repr));
            if (auto* e = item.m_data.opt_Value()) {
                for (auto& var : e->variants) {
                    DEBUG("Enum value " << p << " - " << var.name);

                    if (var.expr) {
                        t_args tmp;
                        ExprVisitorValidate ev(m_resolve, tmp, enum_type);
                        ev.visit_root(var.expr);
                    }
                }
            }
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            auto _ = this->m_resolve.set_impl_generics(MetadataType::TraitObject, item.m_params);
            ::HIR::Visitor::visit_trait(p, item);
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.m_type);
            auto _ = this->m_resolve.set_impl_generics(impl.m_type, impl.m_params);

            ::HIR::Visitor::visit_type_impl(impl);
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " " << trait_path << " for " << impl.m_type);
            auto _ = this->m_resolve.set_impl_generics(impl.m_type, impl.m_params);

            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
        }
    };
}

void TypecheckExpressionsValidateOne(const StaticTraitResolve& resolve, const ::std::vector<::std::pair<::HIR::Pattern, ::HIR::TypeRef>>& args, const ::HIR::TypeData* ret_ty, const ::HIR::ExprPtr& code) {
    ExprVisitorValidate ev(resolve, args, ret_ty);
    ev.expand_erased_types = false; // TODO: Make this an argument, we don't want to do this too early
    ev.visit_root(const_cast<::HIR::ExprPtr&>(code));
}

void TypecheckExpressionsValidate(::HIR::Crate& crate) {
    OuterVisitor ov(crate);
    ov.visit_crate(crate);
}


namespace {

    const ::HIR::GenericParams& get_params_for_item(const Span& sp, const ::HIR::Crate& crate, const ::HIR::SimplePath& path, ::HIR::Visitor::PathContext pc) {
        // Support for enum variants
        if (path.components().size() > 1) {
            const auto& pitem = crate.get_typeitem_by_path(sp, path, false, true);
            if (pitem.is_Enum()) {
                return pitem.as_Enum().m_params;
            }
        }

        switch (pc) {
            case ::HIR::Visitor::PathContext::VALUE: {
                const auto& item = crate.get_valitem_by_path(sp, path);

                TU_MATCH(
                    ::HIR::ValueItem,
                    (item),
                    (e),
                    (Import, BUG(sp, "Value path pointed to import - " << path << " = " << e.path);),
                    (Function, return e.m_params;),
                    (Constant, return e.m_params;),
                    (Static,
                     // TODO: Return an empty set?
                     BUG(sp, "Attepted to get parameters for static " << path);),
                    (StructConstructor, return get_params_for_item(sp, crate, e.ty, ::HIR::Visitor::PathContext::TYPE);),
                    (StructConstant, return get_params_for_item(sp, crate, e.ty, ::HIR::Visitor::PathContext::TYPE);)
                )
            } break;
            case ::HIR::Visitor::PathContext::TRAIT:
                // TODO: treat PathContext::TRAIT differently
            case ::HIR::Visitor::PathContext::TYPE: {
                const auto& item = crate.get_typeitem_by_path(sp, path);

                TU_MATCH(::HIR::TypeItem, (item), (e), (Import, BUG(sp, "Type path pointed to import - " << path);), (TypeAlias, BUG(sp, "Type path pointed to type alias - " << path);), (TraitAlias, BUG(sp, "Type path pointed to trait alias - " << path);), (ExternType, static ::HIR::GenericParams empty_params; return empty_params;), (Module, BUG(sp, "Type path pointed to module - " << path);), (Struct, return e.m_params;), (Enum, return e.m_params;), (Union, return e.m_params;), (Trait, return e.m_params;))
            } break;
        }
        throw "";
    }

    class Visitor: public ::HIR::Visitor {
        ::HIR::Crate& crate;
        StaticTraitResolve m_resolve;

        const ::HIR::Trait* m_current_trait = nullptr;
        const ::HIR::ItemPath* m_current_trait_path = nullptr;

        ::HIR::GenericParams* m_cur_params = nullptr;
        unsigned m_cur_params_level = 0;
        ::HIR::ItemPath* m_fcn_path = nullptr;
        ::HIR::Function* m_fcn_ptr = nullptr;
        unsigned int m_fcn_erased_count = 0;

        ::std::vector<const ::HIR::TypeData*> m_self_types;
        ::std::vector<::HIR::LifetimeRef*> m_current_lifetime;

        typedef ::std::vector<::std::pair<const ::HIR::SimplePath*, const ::HIR::Trait*>> t_trait_imports;
        t_trait_imports m_traits;

    public:
        Visitor(::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.m_types)
            , crate(crate)
            , m_resolve(crate)
        {
        }

    private:
        struct ModTraitsGuard {
            Visitor* v;
            t_trait_imports old_imports;

            ~ModTraitsGuard() {
                this->v->m_traits = mv$(this->old_imports);
            }
        };

        ModTraitsGuard push_mod_traits(const ::HIR::Module& mod) {
            static Span sp;
            DEBUG("");
            auto rv = ModTraitsGuard{this, mv$(this->m_traits)};
            for (const auto& trait_path : mod.m_traits) {
                DEBUG("- " << trait_path);
                m_traits.push_back(::std::make_pair(&trait_path, &this->crate.get_trait_by_path(sp, trait_path)));
            }
            return rv;
        }

        void check_parameters(const Span& sp, const ::HIR::GenericParams& param_def, ::HIR::PathParams& param_vals) {
            MonomorphStatePtr ms(crate.m_types, m_self_types.empty() ? nullptr : m_self_types.back(), &param_vals, nullptr);

            if (param_vals.m_lifetimes.size() == 0) {
                param_vals.m_lifetimes.resize(param_def.m_lifetimes.size());
            }
            if (param_vals.m_lifetimes.size() != param_def.m_lifetimes.size()) {
                ERROR(sp, E0000, "Incorrect lifetime param count, expected " << param_def.m_lifetimes.size() << ", got " << param_vals.m_lifetimes.size());
            }

            while (param_vals.m_types.size() < param_def.m_types.size()) {
                unsigned int i = param_vals.m_types.size();
                const auto& ty_def = param_def.m_types[i];
                if (ty_def.m_default->is_Infer()) {
                    ERROR(sp, E0000, "Unspecified parameter with no default - " << param_def.fmt_args() << " with " << param_vals);
                }

                // Replace and expand
                param_vals.m_types.push_back(ms.monomorph_type(sp, ty_def.m_default));
                DEBUG("Add missing param (using default): " << param_vals.m_types.back());
            }

            if (param_vals.m_types.size() != param_def.m_types.size()) {
                ERROR(sp, E0000, "Incorrect number of parameters - expected " << param_def.m_types.size() << ", got " << param_vals.m_types.size());
            }

            for (unsigned int i = 0; i < param_vals.m_types.size(); i++) {
                if (param_vals.m_types[i] == ::HIR::TypeRef()) {
                    // TODO: Why is this pulling in the default? Why not just leave it as-is

                    //if( param_def.m_types[i].m_default == ::HIR::TypeRef() )
                    //    ERROR(sp, E0000, "Unspecified parameter with no default");
                    // TODO: Monomorphise?
                    param_vals.m_types[i] = ms.monomorph_type(sp, param_def.m_types[i].m_default);
                    DEBUG("Update `_` param (using default): " << param_def.m_types[i].m_default << " -> " << param_vals.m_types[i]);
                }
            }

            // TODO: Check generic bounds
            for (const auto& bound : param_def.m_bounds) {
                TU_MATCH(
                    ::HIR::GenericBound,
                    (bound),
                    (e),
                    (Lifetime, ),
                    (TypeLifetime, ),
                    (
                        TraitBound,
                        // TODO: Check for an implementation of this trait
                        DEBUG("TODO: Check bound " << e.type << " : " << e.trait.m_path);
                        //DEBUG("- " << monomorph_type_with(sp, e.type, monomorph_cb) << " : " << monomorphise_traitpath_with(sp, e.trait, monomorph_cb));
                    ),
                    (TypeEquality,
                     // TODO: Check that two types are equal in this case
                     DEBUG("TODO: Check equality bound " << e.type << " == " << e.other_type);)
                )
            }
        }

    public:
        void visit_lifetime(const Span& sp, HIR::LifetimeRef& lft) {
            if (!lft.is_param()) {
                switch (lft.binding) {
                    case HIR::LifetimeRef::STATIC: // 'static
                        break;
                    case HIR::LifetimeRef::INFER: // '_
                        //TODO(sp, "Handle explicitly elided lifetimes");
                        //break;
                    case HIR::LifetimeRef::UNKNOWN: // <none>
                        // If there's a current liftime (i.e. we're within a borrow), then use that
                        if (!m_current_lifetime.empty() && m_current_lifetime.back()) {
                            lft = *m_current_lifetime.back();
                        }
                        // Otherwise, try to make a new one
                        else if (m_cur_params) {
                            auto idx = m_cur_params->m_lifetimes.size();
                            m_cur_params->m_lifetimes.push_back(HIR::LifetimeDef{RcString::new_interned(FMT("elided#" << idx))});
                            lft.binding = m_cur_params_level * 256 + idx;
                        } else {
                            //ERROR(sp, E0000, "Unspecified lifetime in outer context");
                            // TODO: Would error here, but don't fully support HKTs (e.g. `Fn(&i32)`)
                        }
                        break;
                    default:
                        BUG(sp, "Unexpected lifetime binding - " << lft);
                }
            }
        }

        void visit_path_params(::HIR::PathParams& pp) override {
            static Span _sp;
            const Span& sp = _sp;

            for (auto& lft : pp.m_lifetimes) {
                visit_lifetime(sp, lft);
            }

            HIR::Visitor::visit_path_params(pp);
        }

        void visit_type(::HIR::TypeRef& ty) override {
            static Span _sp;
            const Span& sp = _sp;

            assert(ty);
            auto data = ty->clone_data();

            // Lifetime elision logic!
            if (auto* e = data.opt_Borrow()) {
                visit_lifetime(sp, e->lifetime);
                m_current_lifetime.push_back(&e->lifetime);
            }

            auto self = crate.m_types.self();
            if (data.is_ErasedType()) {
                m_self_types.push_back(self);
            }

            auto saved_params = std::make_pair(m_cur_params, m_cur_params_level);
            if (auto* e = data.opt_Function()) {
                m_cur_params = &e->hrls;
                m_cur_params_level = 3;
            }

            TU_MATCH_HDRA((data), {)
            TU_ARMA(Infer, e) {}
            TU_ARMA(Diverge, e) {}
            TU_ARMA(Primitive, e) {}
            TU_ARMA(Generic, e) {}
            TU_ARMA(Path, e) this->visit_path(e.path, ::HIR::Visitor::PathContext::TYPE);
            TU_ARMA(TraitObject, e) {
                if (e.m_trait.m_path != ::HIR::SimplePath()) this->visit_trait_path(e.m_trait);
                for (auto& marker : e.m_markers) this->visit_generic_path(marker, ::HIR::Visitor::PathContext::TYPE);
            }
            TU_ARMA(ErasedType, e) {
                TU_MATCH_HDRA((e.m_inner), {)
                TU_ARMA(Known, inner) this->visit_type(inner);
                TU_ARMA(Alias, inner) this->visit_path_params(inner.params);
                TU_ARMA(Fcn, inner) if (inner.m_origin != ::HIR::SimplePath()) this->visit_path(inner.m_origin, ::HIR::Visitor::PathContext::VALUE);
                }
                this->visit_path_params(e.m_use);
                for (auto& trait : e.m_traits) this->visit_trait_path(trait);
            }
            TU_ARMA(Array, e) { this->visit_type(e.inner); if (auto* size = e.size.opt_Unevaluated()) this->visit_constgeneric(*size); }
            TU_ARMA(Slice, e) this->visit_type(e.inner);
            TU_ARMA(Tuple, e) for (auto& inner : e) this->visit_type(inner);
            TU_ARMA(Borrow, e) this->visit_type(e.inner);
            TU_ARMA(Pointer, e) this->visit_type(e.inner);
            TU_ARMA(NamedFunction, e) this->visit_path(e.path, ::HIR::Visitor::PathContext::VALUE);
            TU_ARMA(Function, e) { for (auto& arg : e.m_arg_types) this->visit_type(arg); this->visit_type(e.m_rettype); }
            TU_ARMA(NodeType, e) {}
            }

            m_cur_params = saved_params.first;
            m_cur_params_level = saved_params.second;

            if (data.is_ErasedType()) {
                m_self_types.pop_back();
            }

            if (data.is_Borrow()) {
                m_current_lifetime.pop_back();
            }


            if (auto* e = data.opt_TraitObject()) {
                visit_lifetime(sp, e->m_lifetime);
            }

            if (auto* e = data.opt_ErasedType()) {
                for (auto& lft : e->m_lifetime_bounds) visit_lifetime(sp, lft);
            }

            ty = crate.m_types.intern(mv$(data));

            if (const auto* e = ty->opt_Path()) {
                TU_MATCH(::HIR::Path::Data, (e->path.m_data), (pe), (Generic, ), (UfcsUnknown, TODO(sp, "Should UfcsKnown be encountered here?");), (UfcsInherent, TRACE_FUNCTION_FR("UfcsInherent - " << ty, ty); m_resolve.expand_associated_types(sp, ty);), (UfcsKnown, TRACE_FUNCTION_FR("UfcsKnown - " << ty, ty); m_resolve.expand_associated_types(sp, ty);))
            }
        }

        void visit_generic_path(::HIR::GenericPath& p, PathContext pc) override {
            static Span sp;
            TRACE_FUNCTION_F("p = " << p);
            const auto& params = get_params_for_item(sp, crate, p.m_path, pc);
            auto& args = p.m_params;

            check_parameters(sp, params, args);
            DEBUG("p = " << p);

            ::HIR::Visitor::visit_generic_path(p, pc);
        }

    private:
        bool locate_trait_item_in_bounds(const Span& sp, ::HIR::Visitor::PathContext pc, const ::HIR::TypeData* tr, const ::HIR::GenericParams& params, ::HIR::Path::Data& pd) {
            //const auto& name = pd.as_UfcsUnknown().item;
            for (const auto& b : params.m_bounds) {
                TU_IFLET(::HIR::GenericBound, b, TraitBound, e, DEBUG("- " << e.type << " : " << e.trait.m_path); if (e.type == tr) {
                    DEBUG(" - Match");
                    if (locate_in_trait_and_set(sp, pc, e.trait.m_path, this->crate.get_trait_by_path(sp, e.trait.m_path.m_path), pd)) {
                        return true;
                    }
                });
                // -
            }
            return false;
        }

        static ::HIR::Path::Data get_ufcs_known(::HIR::Path::Data::Data_UfcsUnknown e, ::HIR::GenericPath trait_path, const ::HIR::Trait& trait) {
            return ::HIR::Path::Data::make_UfcsKnown({mv$(e.type), mv$(trait_path), mv$(e.item), mv$(e.params)});
        }

        static bool locate_item_in_trait(::HIR::Visitor::PathContext pc, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            const auto& e = pd.as_UfcsUnknown();

            switch (pc) {
                case ::HIR::Visitor::PathContext::VALUE:
                    if (trait.m_values.find(e.item) != trait.m_values.end()) {
                        return true;
                    }
                    break;
                case ::HIR::Visitor::PathContext::TRAIT:
                    break;
                case ::HIR::Visitor::PathContext::TYPE:
                    if (trait.m_types.find(e.item) != trait.m_types.end()) {
                        return true;
                    }
                    break;
            }
            return false;
        }

        bool locate_in_trait_and_set(const Span& sp, ::HIR::Visitor::PathContext pc, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            if (locate_item_in_trait(pc, trait, pd)) {
                pd = get_ufcs_known(mv$(pd.as_UfcsUnknown()), make_generic_path(trait_path.m_path, trait), trait);
                return true;
            }
            // Search all supertraits
            for (const auto& pt : trait.m_all_parent_traits) {
                if (locate_item_in_trait(pc, *pt.m_trait_ptr, pd)) {
                    pd = get_ufcs_known(mv$(pd.as_UfcsUnknown()), make_generic_path(trait_path.m_path, trait), trait);
                    return true;
                }
            }
            return false;
        }

        bool set_from_impl(const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            const auto& type = e.type;
            return this->crate.find_trait_impls(trait_path.m_path, type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                DEBUG("FOUND impl" << impl.m_params.fmt_args() << " " << trait_path.m_path << impl.m_trait_args << " for " << impl.m_type);
                // TODO: Check bounds
                for (const auto& bound : impl.m_params.m_bounds) {
                    DEBUG("- TODO: Bound " << bound);
                    return false;
                }
                pd = get_ufcs_known(mv$(e), make_generic_path(trait_path.m_path, trait), trait);
                return true;
            });
        }

        bool locate_in_trait_impl_and_set(::HIR::Visitor::PathContext pc, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            if (this->locate_item_in_trait(pc, trait, pd)) {
                return this->set_from_impl(trait_path, trait, pd);
            } else {
                DEBUG("- Item " << e.item << " not in trait " << trait_path.m_path);
            }

            // Search supertraits (recursively)
            for (const auto& pt : trait.m_all_parent_traits) {
                if (this->locate_item_in_trait(pc, *pt.m_trait_ptr, pd)) {
                    // TODO: Monomorphise params?
                    return set_from_impl(pt.m_path, *pt.m_trait_ptr, pd);
                } else {
                }
            }
            return false;
        }

        ::HIR::GenericPath make_generic_path(::HIR::SimplePath sp, const ::HIR::Trait& trait) {
            auto trait_path_g = ::HIR::GenericPath(mv$(sp));
            for (unsigned int i = 0; i < trait.m_params.m_types.size(); i++) {
                trait_path_g.m_params.m_types.push_back(crate.m_types.generic(trait.m_params.m_types[i].m_name, i));
            }
            return trait_path_g;
        }

        ::HIR::GenericPath get_current_trait_gp() const {
            assert(m_current_trait_path);
            assert(m_current_trait);
            auto trait_path = ::HIR::GenericPath(m_current_trait_path->get_simple_path());
            for (unsigned int i = 0; i < m_current_trait->m_params.m_types.size(); i++) {
                trait_path.m_params.m_types.push_back(crate.m_types.generic(m_current_trait->m_params.m_types[i].m_name, i));
            }
            return trait_path;
        }

        void visit_path_UfcsUnknown(const Span& sp, ::HIR::Path& p, ::HIR::Visitor::PathContext pc) {
            TRACE_FUNCTION_FR("UfcsUnknown - p=" << p, p);
            auto& e = p.m_data.as_UfcsUnknown();

            this->visit_type(e.type);
            this->visit_path_params(e.params);

            // Search for matching impls in current generic blocks
            if (m_resolve.m_item_generics != nullptr && locate_trait_item_in_bounds(sp, pc, e.type, *m_resolve.m_item_generics, p.m_data)) {
                return;
            }
            if (m_resolve.m_impl_generics != nullptr && locate_trait_item_in_bounds(sp, pc, e.type, *m_resolve.m_impl_generics, p.m_data)) {
                return;
            }

            if (const auto* te = e.type->opt_Generic()) {
                // If processing a trait, and the type is 'Self', search for the type/method on the trait
                // - TODO: This could be encoded by a `Self: Trait` bound in the generics, but that may have knock-on issues?
                if (te->name == "Self" && m_current_trait) {
                    auto trait_path = this->get_current_trait_gp();
                    if (this->locate_in_trait_and_set(sp, pc, trait_path, *m_current_trait, p.m_data)) {
                        // Success!
                        return;
                    }
                }
                ERROR(sp, E0000, "Failed to find impl with '" << e.item << "' for " << e.type);
                return;
            } else {
                // 1. Search for applicable inherent methods (COMES FIRST!)
                if (this->crate.find_type_impls(e.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                    DEBUG("- matched inherent impl " << e.type);
                    // Search for item in this block
                    switch (pc) {
                        case ::HIR::Visitor::PathContext::VALUE:
                            if (impl.m_methods.find(e.item) == impl.m_methods.end()) {
                                return false;
                            }
                            // Found it, just keep going (don't care about details here)
                            break;
                        case ::HIR::Visitor::PathContext::TRAIT:
                            return false;
                        case ::HIR::Visitor::PathContext::TYPE:
                            if (impl.m_types.find(e.item) == impl.m_types.end()) {
                                return false;
                            }
                            break;
                    }

                    return true;
                })) {
                    auto new_data = ::HIR::Path::Data::make_UfcsInherent({mv$(e.type), mv$(e.item), mv$(e.params)});
                    p.m_data = mv$(new_data);
                    DEBUG("- Resolved, replace with " << p);
                    return;
                }
                // 2. Search all impls of in-scope traits for this method on this type
                for (const auto& trait_info : m_traits) {
                    const auto& trait = *trait_info.second;

                    switch (pc) {
                        case ::HIR::Visitor::PathContext::VALUE:
                            if (trait.m_values.find(e.item) == trait.m_values.end()) {
                                continue;
                            }
                            break;
                        case ::HIR::Visitor::PathContext::TRAIT:
                        case ::HIR::Visitor::PathContext::TYPE:
                            if (trait.m_types.find(e.item) == trait.m_types.end()) {
                                continue;
                            }
                            break;
                    }
                    DEBUG("- Trying trait " << *trait_info.first);

                    auto trait_path = ::HIR::GenericPath(*trait_info.first);
                    for (unsigned int i = 0; i < trait.m_params.m_types.size(); i++) {
                        trait_path.m_params.m_types.push_back(crate.m_types.infer());
                    }

                    // TODO: Search supertraits
                    // TODO: Should impls be searched first, or item names?
                    // - Item names add complexity, but impls are slower
                    if (this->locate_in_trait_impl_and_set(pc, mv$(trait_path), trait, p.m_data)) {
                        return;
                    }
                }
            }

            // Couldn't find it
            ERROR(sp, E0000, "Failed to find impl with '" << e.item << "' for " << e.type << " (in " << p << ")");
        }

    public:
        void visit_expr(HIR::ExprPtr& exp) override {
            // No-op
        }

        void visit_path(::HIR::Path& p, ::HIR::Visitor::PathContext pc) override {
            //assert(pc == ::HIR::Visitor::PathContext::TYPE);
            TU_MATCH(
                ::HIR::Path::Data,
                (p.m_data),
                (e),
                (Generic, this->visit_generic_path(e, pc);),
                (
                    UfcsKnown, this->visit_type(e.type); m_self_types.push_back(e.type); this->visit_generic_path(e.trait, ::HIR::Visitor::PathContext::TRAIT); m_self_types.pop_back();
                    // TODO: Locate impl block and check parameters
                ),
                (
                    UfcsInherent, this->visit_type(e.type);
                    // TODO: Locate impl block and check parameters
                ),
                (UfcsUnknown, BUG(Span(), "Encountered unknown-trait UFCS path during outer typeck - " << p);)
            )
        }

        void visit_params(::HIR::GenericParams& params) override {
            TRACE_FUNCTION_F(params.fmt_args());
            for (auto& tps : params.m_types) {
                this->visit_type(tps.m_default);
            }

            for (auto& bound : params.m_bounds) {
                TU_MATCH_HDRA( (bound), {)
                TU_ARMA(Lifetime, e) {
                    }
                    TU_ARMA(TypeLifetime, e) {
                        this->visit_type(e.type);
                    }
                    TU_ARMA(TraitBound, e) {
                        this->visit_type(e.type);
                        m_self_types.push_back(e.type);
                        this->visit_trait_path(e.trait);
                        m_self_types.pop_back();
                    }
                    //(NotTrait, e) {
                    //    ::HIR::TypeRef  type;
                    //    ::HIR::GenricPath    trait;
                    //    }),
                    TU_ARMA(TypeEquality, e) {
                        this->visit_type(e.type);
                        this->visit_type(e.other_type);
                    }
                }
            }
        }

        void visit_module(::HIR::ItemPath p, ::HIR::Module& mod) override {
            auto _ = this->push_mod_traits(mod);
            ::HIR::Visitor::visit_module(p, mod);
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            m_current_trait = &item;
            m_current_trait_path = &p;

            auto _ = m_resolve.set_impl_generics(MetadataType::TraitObject, item.m_params);
            auto self = crate.m_types.self();
            m_self_types.push_back(self);
            ::HIR::Visitor::visit_trait(p, item);
            m_self_types.pop_back();

            m_current_trait = nullptr;
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            auto _ = m_resolve.set_impl_generics(item.m_struct_markings.dst_type, item.m_params);
            ::HIR::Visitor::visit_struct(p, item);
        }

        void visit_union(::HIR::ItemPath p, ::HIR::Union& item) override {
            auto _ = m_resolve.set_impl_generics(MetadataType::None, item.m_params);
            ::HIR::Visitor::visit_union(p, item);
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = m_resolve.set_impl_generics(MetadataType::None, item.m_params);
            ::HIR::Visitor::visit_enum(p, item);
        }

        void visit_associatedtype(::HIR::ItemPath p, ::HIR::AssociatedType& item) override {
            // Push `Self = <Self as CurTrait>::Type` for processing defaults in the bounds.
            auto path_aty = ::HIR::Path(crate.m_types.self(), this->get_current_trait_gp(), p.get_name());
            auto ty_aty = crate.m_types.path(mv$(path_aty), ::HIR::TypePathBinding::make_Opaque({}));
            m_self_types.push_back(ty_aty);

            ::HIR::Visitor::visit_associatedtype(p, item);

            m_self_types.pop_back();
        }

        void visit_type_alias(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            // Ignore type aliases, they don't have to typecheck.
        }

        void visit_inherent_type(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            auto _ = m_resolve.set_item_generics(item.m_params);
            auto saved_params = std::make_pair(m_cur_params, m_cur_params_level);
            m_cur_params = &item.m_params;
            m_cur_params_level = 1;
            ::HIR::Visitor::visit_inherent_type(p, item);
            m_cur_params = saved_params.first;
            m_cur_params_level = saved_params.second;
        }

        void add_lifetime_bounds_for_impl_type(const Span& sp, HIR::GenericParams& dst, const ::HIR::TypeData* ty) {
            // REF: rustc-1.29.0-src/src/vendor/clap/src/args/arg.rs:54 - Omitted lifetime bounds

            // https://rust-lang.github.io/rfcs/2089-implied-bounds.html ?
            // HACK: Just grab the lifetime bounds from a path type
            if (ty->is_Path() && ty->as_Path().path.m_data.is_Generic()) {
                const auto& gp = ty->as_Path().path.m_data.as_Generic();
                const auto& ti = m_resolve.m_crate.get_typeitem_by_path(sp, gp.m_path);

                const HIR::GenericParams* params = nullptr;
                if (const auto* e = ti.opt_Struct()) {
                    params = &e->m_params;
                } else if (const auto* e = ti.opt_Enum()) {
                    params = &e->m_params;
                } else if (const auto* e = ti.opt_Union()) {
                    params = &e->m_params;
                } else {
                    DEBUG("TODO: Obtain bounds from " << ti.tag_str());
                }

                if (params) {
                    MonomorphStatePtr ms(crate.m_types, nullptr, &gp.m_params, nullptr);
                    for (const auto& b : params->m_bounds) {
                        if (const auto* be = b.opt_Lifetime()) {
                            dst.m_bounds.push_back(HIR::GenericBound::make_Lifetime({ms.monomorph_lifetime(sp, be->test), ms.monomorph_lifetime(sp, be->valid_for)}));
                        }
                    }
                }
            }
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.m_type);
            auto _ = m_resolve.set_impl_generics(impl.m_type, impl.m_params);
            m_self_types.push_back(impl.m_type);

            // Pre-visit so lifetime elision can work
            {
                m_cur_params = &impl.m_params;
                m_cur_params_level = 0;
                this->visit_type(impl.m_type);
                m_cur_params = nullptr;
            }

            // Propagate bounds from the type
            add_lifetime_bounds_for_impl_type(Span(), impl.m_params, impl.m_type);

            ::HIR::Visitor::visit_type_impl(impl);
            // TODO: Check that the type is valid

            m_self_types.pop_back();
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            static Span sp;
            TRACE_FUNCTION_F("impl" << impl.m_params.fmt_args() << " " << trait_path << impl.m_trait_args << " for " << impl.m_type);
            auto _ = m_resolve.set_impl_generics(impl.m_type, impl.m_params);
            m_self_types.push_back(impl.m_type);

            // Pre-visit so lifetime elision can work
            {
                m_cur_params = &impl.m_params;
                m_cur_params_level = 0;
                this->visit_type(impl.m_type);
                this->visit_path_params(impl.m_trait_args);
                m_cur_params = nullptr;
            }

            // Propagate bounds from the type
            add_lifetime_bounds_for_impl_type(Span(), impl.m_params, impl.m_type);

            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            m_self_types.pop_back();

            // TODO: Check that the type+trait is valid
            // - And fix bad elided liftimes (match annotations if they were elided)
            {
                const auto& trait = m_resolve.m_crate.get_trait_by_path(sp, trait_path);
                for (auto& e : impl.m_methods) {
                    auto _ = m_resolve.set_item_generics(e.second.data.m_params);

                    const auto v_it = trait.m_values.find(e.first);
                    if (v_it == trait.m_values.end() || !v_it->second.is_Function()) {
                        ERROR(sp, E0000, "Trait " << trait_path << " doesn't have a method named " << e.first);
                    }
                    auto& impl_fcn = e.second.data;
                    const auto& trait_fcn = v_it->second.as_Function();

                    auto fcn_params = trait_fcn.m_params.make_nop_params(crate.m_types, 1);
                    MonomorphStatePtr ms{crate.m_types, impl.m_type, &impl.m_trait_args, &fcn_params};
                    HIR::TypeRef tmp;
                    auto maybe_monomorph = [&](const HIR::TypeData* ty) -> const HIR::TypeData* {
                        if (monomorphise_type_needed(ty)) {
                            tmp = ms.monomorph_type(sp, ty);
                            m_resolve.expand_associated_types(sp, tmp);
                            return tmp;
                        } else {
                            return ty;
                        }
                    };

                    // Check signature
                    // - Includes fixing incorrectly elided lifetimes
                    // ```
                    // trait Foo<T> {
                    //   fn foo(&self, bar: T);
                    // }
                    // impl Foo<&Bar> for Baz {
                    //   fn foo(&self, bar: &Bar) { }
                    // }

                    std::vector<std::string> failures;
                    // -- Generics
                    if (impl_fcn.m_params.m_types.size() != trait_fcn.m_params.m_types.size()) {
                        failures.push_back(FMT("Mismatched type param count (expected " << trait_fcn.m_params.m_types.size() << ", got " << impl_fcn.m_params.m_types.size() << ")"));
                    }
                    // Different logic for lifetimes, only want to check un-elided lifetimes
                    // - Well, elided lifetimes can overlap non-elided ones (as long as they're identical)
                    if (impl_fcn.m_params.m_values.size() != trait_fcn.m_params.m_values.size()) {
                        failures.push_back(FMT("Mismatched const param count (expected " << trait_fcn.m_params.m_values.size() << ", got " << impl_fcn.m_params.m_values.size() << ")"));
                    }
                    // -- Arguments
                    if (impl_fcn.m_args.size() != trait_fcn.m_args.size()) {
                        failures.push_back(FMT("Mismatched argument count (expected " << trait_fcn.m_args.size() << ", got " << impl_fcn.m_args.size() << ")"));
                    }
                    if (impl_fcn.m_receiver != trait_fcn.m_receiver) {
                        failures.push_back(FMT("Receiver type")); //"(expected " << trait_fcn.m_receiver << ", got " << impl_fcn.m_receiver));
                    }
                    for (size_t i = 0; i < std::min(impl_fcn.m_args.size(), trait_fcn.m_args.size()); i++) {
                        if (!(i == 0 && (trait_fcn.m_receiver == HIR::Function::Receiver::Free || impl_fcn.m_receiver == HIR::Function::Receiver::Free))) {
                            // Check the type.
                            // - Also, fix lifetime elision?
                            const auto& exp_ty = maybe_monomorph(trait_fcn.m_args[i].second);
                            /*const*/ auto& has_ty = impl_fcn.m_args[i].second;

                            if (exp_ty != has_ty && !exp_ty->equals_ignoring_regions(has_ty)) {
                                failures.push_back(FMT("Argument " << 1 + i << " mismatch - expected " << exp_ty << ", got " << has_ty));
                            }
                        }
                    }

                    // Handle `implTrait` in returns
                    // - Would need to re-create `exp_ret_ty` to keep the `impl Trait`, OR keep a non-erased/expanded copy of the type
                    // > The difference tends to be in lifetimes, so match the two types and update lifetimes?
                    struct MCB: public ::HIR::MatchGenerics {
                        ::std::map<RcString, const ::HIR::TypeData*> mapping;

                        ::HIR::Compare cmp_type(const Span& sp, const ::HIR::TypeData* ty_l, const ::HIR::TypeData* ty_r, HIR::t_cb_resolve_type resolve_cb) override {
                            // If the LHS is an ATY that starts with `erased#` then just accept it?
                            // - Also record the mapping
                            if (const auto* ty_p = ty_l->opt_Path()) {
                                if (const auto* path_p = ty_p->path.m_data.opt_UfcsKnown()) {
                                    if (path_p->item.compare(0, strlen(ATY_PREFIX_ERASED), ATY_PREFIX_ERASED) == 0) {
                                        mapping.insert(std::make_pair(path_p->item, ty_r));
                                        return ::HIR::Compare::Equal;
                                    }
                                }
                            }
                            return ::HIR::MatchGenerics::cmp_type(sp, ty_l, ty_r, resolve_cb);
                        }

                        ::HIR::Compare match_ty(const ::HIR::GenericRef& g, const ::HIR::TypeData* ty, HIR::t_cb_resolve_type resolve_cb) override {
                            return (!ty->is_Generic() || ty->as_Generic() != g) ? ::HIR::Compare::Unequal : ::HIR::Compare::Equal;
                        }

                        ::HIR::Compare match_val(const ::HIR::GenericRef& g, const ::HIR::ConstGeneric& sz) override {
                            return (!sz.is_Generic() || sz.as_Generic() != g) ? ::HIR::Compare::Unequal : ::HIR::Compare::Equal;
                        }
                    } match_cb;

                    const auto& exp_ret_ty1 = maybe_monomorph(trait_fcn.m_return);
                    if (!exp_ret_ty1->match_test_generics(sp, impl_fcn.m_return, HIR::ResolvePlaceholdersNop(), match_cb)) {
                        failures.push_back(
                            FMT("Mismatched return type:\n"
                                << "  Expected " << exp_ret_ty1 << "\n"
                                << "  Found    " << impl_fcn.m_return)
                        );
                    }
                    HIR::TypeRef exp_ret_ty_real;
                    const auto& exp_ret_ty = match_cb.mapping.empty() ? exp_ret_ty1 : (exp_ret_ty_real = clone_ty_with(crate.m_types, sp, exp_ret_ty1, [&](const ::HIR::TypeData* ref, ::HIR::TypeRef& out) -> bool {
                        if (const auto* ty_p = ref->opt_Path()) {
                            if (const auto* path_p = ty_p->path.m_data.opt_UfcsKnown()) {
                                auto it = match_cb.mapping.find(path_p->item);
                                if (it != match_cb.mapping.end()) {
                                    out = it->second;
                                    return true;
                                }
                            }
                        }
                        return false;
                    }));

                    //if( impl_fcn.m_params.m_lifetimes.size() != trait_fcn.m_params.m_lifetimes.size() ) {
                    //    failures.push_back(FMT("Mismatched lifetime param count (expected " << trait_fcn.m_params.m_lifetimes.size() << ", got " << impl_fcn.m_params.m_lifetimes.size() << ")"));
                    //}

                    if (!failures.empty()) {
                        ERROR(
                            sp,
                            E0000,
                            "Method " << e.first << " doesn't match trait:\n"
                                      << FMT_CB(os, for (const auto& f : failures) os << "- " << f << "\n") << "Trait:\n"
                                      << FMT_CB(
                                             os,
                                             {
                                                 os << "    fn " << e.first << trait_fcn.m_params.fmt_args() << "(";
                                                 for (const auto& a : trait_fcn.m_args) {
                                                     os << a.first << ": " << maybe_monomorph(a.second) << ", ";
                                                 }
                                                 os << ")\n";
                                                 os << "    -> " << maybe_monomorph(trait_fcn.m_return) << "\n";
                                                 os << "    " << trait_fcn.m_params.fmt_bounds();
                                             }
                                         )
                                      << "\n"
                                      << "Impl :\n"
                                      << FMT_CB(
                                             os,
                                             {
                                                 os << "    fn " << e.first << impl_fcn.m_params.fmt_args() << "(";
                                                 for (const auto& a : impl_fcn.m_args) {
                                                     os << a.first << ": " << a.second << ", ";
                                                 }
                                                 os << ")\n";
                                                 os << "    -> " << impl_fcn.m_return << "\n";
                                                 os << "    " << impl_fcn.m_params.fmt_bounds();
                                             }
                                         )
                                      << "\n"
                                      << "in impl" << impl.m_params.fmt_args() << " " << trait_path << impl.m_trait_args << " for " << impl.m_type
                        );
                    }
                    // HACK: Replace all types (which should be functionally identical) so lifetimes match
                    // - This is needed for monomorphisation to work properly?
                    // REF: rustc-1.29.0/src/vendor/serde/src/private/de.rs:1379
                    // Counter-ref: rustc-1.54.0
                    // Update AFTER the checks
                    DEBUG("Replace generic block's lifetimes with " << trait_fcn.m_params.fmt_args());
                    impl_fcn.m_params.m_lifetimes = trait_fcn.m_params.m_lifetimes;
                    // Replace the lifetime bounds too (undoes some potential confusion from elision)
                    {
                        auto& bl = impl_fcn.m_params.m_bounds;
                        bl.erase(
                            std::remove_if(
                                bl.begin(),
                                bl.end(),
                                [](const HIR::GenericBound& b) {
                            return b.is_Lifetime();
                        }
                            ),
                            bl.end()
                        );
                    }
                    for (const auto& b : trait_fcn.m_params.m_bounds) {
                        TU_MATCH_HDRA( (b), { )
                        default:
                            break;
                            TU_ARMA(TypeLifetime, be) {
                                impl_fcn.m_params.m_bounds.push_back(::HIR::GenericBound::make_TypeLifetime({ms.monomorph_type(sp, be.type), ms.monomorph_lifetime(sp, be.valid_for)}));
                            }
                            TU_ARMA(Lifetime, be) {
                                impl_fcn.m_params.m_bounds.push_back(::HIR::GenericBound::make_Lifetime({ms.monomorph_lifetime(sp, be.test), ms.monomorph_lifetime(sp, be.valid_for)}));
                            }
                        }
                    }

                    // HACK: Clone the expected type, so the lifetimes match.
                    DEBUG("Updating < " << impl.m_type << " as " << trait_path << impl.m_trait_args << " >::" << e.first);
                    impl_fcn.m_return = exp_ret_ty;
                    for (size_t i = 0; i < std::min(impl_fcn.m_args.size(), trait_fcn.m_args.size()); i++) {
                        DEBUG("ARG" << i << "> " << trait_fcn.m_args[i].second);
                        impl_fcn.m_args[i].second = m_resolve.monomorph_expand(sp, trait_fcn.m_args[i].second, ms);
                    }
                    DEBUG("Updated < " << impl.m_type << " as " << trait_path << impl.m_trait_args << " >::" << e.first);

                    DEBUG(FMT_CB(os, {
                        os << "fn " << e.first << impl_fcn.m_params.fmt_args() << "(";
                        for (const auto& a : impl_fcn.m_args) {
                            os << a.first << ": " << a.second << ", ";
                        }
                        os << ")";
                        os << impl_fcn.m_params.fmt_bounds();
                    }));
                }
                for (const auto& e : impl.m_constants) {
                    const auto& vi = trait.m_values.at(e.first);
                    if (!vi.is_Constant()) {
                        ERROR(sp, E0000, "Trait " << trait_path << " doesn't have a constant named " << e.first);
                    }
                    const auto& impl_const = e.second.data;
                    const auto& trait_const = vi.as_Constant();

                    // Check type
                }
                for (const auto& e : impl.m_statics) {
                    const auto& vi = trait.m_values.at(e.first);
                    if (!vi.is_Static()) {
                        ERROR(sp, E0000, "Trait " << trait_path << " doesn't have a static named " << e.first);
                    }
                    const auto& impl_static = e.second.data;
                    const auto& trait_static = vi.as_Static();

                    // Check type
                }
                for (const auto& e : trait.m_types) {
                    const auto& trait_type = trait.m_types.at(e.first);
                    const auto& impl_type = e.second;

                    // Check that the bounds fit
                }
            }
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.m_type << " { }");
            auto _ = m_resolve.set_impl_generics(impl.m_type, impl.m_params);
            m_self_types.push_back(impl.m_type);

            // Pre-visit so lifetime elision can work
            {
                m_cur_params = &impl.m_params;
                m_cur_params_level = 0;
                this->visit_type(impl.m_type);
                this->visit_path_params(impl.m_trait_args);
                m_cur_params = nullptr;
            }

            // Propagate bounds from the type/trait
            add_lifetime_bounds_for_impl_type(Span(), impl.m_params, impl.m_type);

            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
            // TODO: Check that the type+trait is valid

            m_self_types.pop_back();
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            TRACE_FUNCTION_F(p);

            if (m_resolve.m_crate.get_lang_item_path_opt("sized").components().empty()) {
                ERROR(Span(), E0000, "requires `sized` lang_item");
            }

            auto _ = m_resolve.set_item_generics(item.m_params);
            // NOTE: Superfluous... except that it makes the params valid for the return type.
            visit_params(item.m_params);

            m_fcn_ptr = &item;
            auto first_elided_lifetime_idx = item.m_params.m_lifetimes.size();

            // Visit arguments
            // - Used to convert `impl Trait` in argument position into generics
            // - Done first so the path in return-position `impl Trait` is valid
            m_cur_params = &item.m_params;
            m_cur_params_level = 1;
            for (auto& arg : item.m_args) {
                TRACE_FUNCTION_F("ARG " << arg);
                visit_type(arg.second);
            }
            m_cur_params = nullptr;

            // Get output lifetime
            // - Try `&self`'s lifetime (if it was an elided lifetime)
            HIR::LifetimeRef elided_output_lifetime;
            if (item.m_receiver != HIR::Function::Receiver::Free) {
                if (const auto* b = item.m_args[0].second->opt_Borrow()) {
                    // If this was an elided lifetime.
                    if (b->lifetime.is_param() && (b->lifetime.binding >> 8) == 1 && (b->lifetime.binding & 0xFF) > first_elided_lifetime_idx) {
                        elided_output_lifetime = b->lifetime;
                    }
                }
            }
            // - OR, look for only one elided lifetime
            if (elided_output_lifetime == HIR::LifetimeRef()) {
                if (item.m_params.m_lifetimes.size() == first_elided_lifetime_idx + 1) {
                    elided_output_lifetime = HIR::LifetimeRef(256 + first_elided_lifetime_idx);
                }
            }
            // If present, set it (push to the stack)
            assert(m_current_lifetime.empty());
            if (elided_output_lifetime != HIR::LifetimeRef()) {
                m_current_lifetime.push_back(&elided_output_lifetime);
            }

            // Visit return type (populates path for `impl Trait` in return position
            m_fcn_path = &p;
            m_fcn_erased_count = 0;
            {
                TRACE_FUNCTION_F("RET " << item.m_return);
                visit_type(item.m_return);
            }
            m_fcn_path = nullptr;
            m_fcn_ptr = nullptr;

            if (elided_output_lifetime != HIR::LifetimeRef()) {
                m_current_lifetime.pop_back();
            }
            assert(m_current_lifetime.empty());

            if (item.m_receiver == HIR::Function::Receiver::Custom) {
                ASSERT_BUG(Span(), item.m_receiver_type, "Custom receiver without a receiver type");
                this->visit_type(*item.m_receiver_type);
            }
            ::HIR::Visitor::visit_function(p, item);
        }
    };
}

void TypecheckModuleLevel(::HIR::Crate& crate) {
    Visitor v{crate};
    v.visit_crate(crate);
}
