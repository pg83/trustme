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
        const StaticTraitResolve& mResolve;
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
        ::std::vector<const ::HIR::ExprNodeLoop*> loops;
        //const ::HIR::ExprPtr* m_cur_expr;

        ::HIR::SimplePath mLangIndex;

    public:
        bool expand_erased_types;

        ExprVisitorValidate(const StaticTraitResolve& res, const t_args& args, const ::HIR::TypeData* ret_type)
            : mResolve(res)
            ,
            //m_args(args),
            real_ret_type(ret_type)
            , expand_erased_types(true)
        {
            mLangIndex = mResolve.crate.get_lang_item_path_opt("index");
        }

        void visit_root(::HIR::ExprPtr& node_ptr) {
            const auto& sp = node_ptr->span();

            // Monomorphise erased type
            ret_type = clone_ty_with(mResolve.crate.types, sp, real_ret_type, [&](const auto& tpl, auto& rv) -> bool {
                if (const auto* e = tpl->opt_ErasedType()) {
                    if (const auto* ee = e->inner.opt_Fcn()) {
                        ASSERT_BUG(sp, ee->index < node_ptr.erasedTypes.size(), "Erased type index OOB - " << ee->origin << " " << ee->index << " >= " << node_ptr.erasedTypes.size());
                        // TODO: Check that erased type bounds are still met
                        rv = node_ptr.erasedTypes[ee->index];
                        return true;
                    }
                }
                return false;
            });
            mResolve.expand_associated_types(sp, ret_type);

            node_ptr->visit(*this);

            check_types_equal(sp, ret_type, node_ptr->resType);
        }

        void visit(::HIR::ExprNodeBlock& node) override {
            TRACE_FUNCTION_F(&node << " { ... }");
            for (auto& n : node.nodes) {
                n->visit(*this);
            }
            if (node.valueNode) {
                node.valueNode->visit(*this);
                check_types_equal(node.span(), node.resType, node.valueNode->resType);
            }
        }

        void visit(::HIR::ExprNodeConstBlock& node) override {
            TRACE_FUNCTION_F(&node << " const { ... }");
            node.inner->visit(*this);
            check_types_equal(node.span(), node.resType, node.inner->resType);
        }

        void visit(::HIR::ExprNodeAsm& node) override {
            TRACE_FUNCTION_F(&node << " llvm_asm! ...");

            // TODO: Check result types
            for (auto& v : node.outputs) {
                v.value->visit(*this);
            }
            for (auto& v : node.inputs) {
                v.value->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeAsm2& node) override {
            TRACE_FUNCTION_F(&node << " asm! ...");

            // TODO: Check result types
            for (auto& v : node.mParams) {
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
            check_types_equal(ret_ty, node.mValue);
            node.mValue->visit(*this);
        }

        void visit(::HIR::ExprNodeYield& node) override {
            TRACE_FUNCTION_F(&node << " yield ...");
            ASSERT_BUG(node.span(), !this->closure_ret_types.empty(), "Yield outside a generator closure");
            ASSERT_BUG(node.span(), this->closure_ret_types.back().yield_type, "Yield outside a generator closure");
            check_types_equal(this->closure_ret_types.back().yield_type, node.mValue);
            node.mValue->visit(*this);
        }

        void visit(::HIR::ExprNodeAWait& node) override {
            node.mValue->visit(*this);
            auto t = mResolve.crate.types.path(::HIR::Path(node.mValue->resType, mResolve.mLangFuture, "Output"), {});
            mResolve.expand_associated_types(node.span(), t);
            check_types_equal(node.span(), node.resType, t);
        }

        void visit(::HIR::ExprNodeLoop& node) override {
            TRACE_FUNCTION_F(&node << " loop { ... }");
            loops.push_back(&node);
            node.mCode->visit(*this);
            loops.pop_back();
        }

        void visit(::HIR::ExprNodeLoopControl& node) override {
            TRACE_FUNCTION_F(&node << " " << (node.isContinue ? "continue" : "break") << " '" << node.label);

            if (node.mValue) {
                node.mValue->visit(*this);
            }

            if (!node.isContinue) {
                ::HIR::TypeRef unit = mResolve.crate.types.unit();
                const auto& ty = (node.mValue ? node.mValue->resType : unit);

                auto it = ::std::find(this->loops.rbegin(), this->loops.rend(), node.targetNode);
                ASSERT_BUG(node.span(), it != this->loops.rend(), "Loop target node not found in the loop stack");

                DEBUG("Breaking to " << node.targetNode << ", type " << node.targetNode->resType);
                check_types_equal(node.span(), node.targetNode->resType, ty);
            }
        }

        void visit(::HIR::ExprNodeLet& node) override {
            TRACE_FUNCTION_F(&node << " let " << node.pattern << ": " << node.mType);
            if (node.mValue) {
                check_pattern(node.pattern, node.mValue->resType);
                check_types_equal(node.span(), node.mType, node.mValue->resType);
                node.mValue->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeMatch& node) override {
            TRACE_FUNCTION_F(&node << " match ...");
            node.mValue->visit(*this);
            for (auto& arm : node.arms) {
                for (const auto& pat : arm.patterns) {
                    check_pattern(pat, node.mValue->resType);
                }
                check_types_equal(node.span(), node.resType, arm.mCode->resType);
                arm.mCode->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeAssign& node) override {
            TRACE_FUNCTION_F(&node << "... ?= ...");

            if (node.op == ::HIR::ExprNodeAssign::Op::None) {
                check_types_equal(node.span(), node.slot->resType, node.mValue->resType);
            } else {
                // Type inferrence using the +=
                // - "" as type name to indicate that it's just using the trait magic?
                const char* lang_item = nullptr;
                auto operator_kind = typeck::PrimitiveOperator::None;
                switch (node.op) {
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
                if (!typeck::primitive_operator_has_builtin(operator_kind, node.slot->resType, node.mValue->resType)) {
                    const auto& trait_path = this->get_lang_item_path(node.span(), lang_item);
                    check_trait_bound(node.span(), trait_path, {node.mValue->resType}, node.slot->resType);
                }
            }

            node.slot->visit(*this);
            node.mValue->visit(*this);
        }

        void visit(::HIR::ExprNodeBinOp& node) override {
            TRACE_FUNCTION_F(&node << "... " << ::HIR::ExprNodeBinOp::opname(node.op) << " ...");

            switch (node.op) {
                case ::HIR::ExprNodeBinOp::Op::CmpEqu:
                case ::HIR::ExprNodeBinOp::Op::CmpNEqu:
                case ::HIR::ExprNodeBinOp::Op::CmpLt:
                case ::HIR::ExprNodeBinOp::Op::CmpLtE:
                case ::HIR::ExprNodeBinOp::Op::CmpGt:
                case ::HIR::ExprNodeBinOp::Op::CmpGtE: {
                    check_types_equal(node.span(), mResolve.crate.types.primitive(::HIR::CoreType::Bool), node.resType);

                    const char* item_name = nullptr;
                    switch (node.op) {
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
                    auto operator_kind = node.op == ::HIR::ExprNodeBinOp::Op::CmpEqu || node.op == ::HIR::ExprNodeBinOp::Op::CmpNEqu
                        ? typeck::PrimitiveOperator::Equal
                        : typeck::PrimitiveOperator::Order;
                    if (!typeck::primitive_operator_has_builtin(operator_kind, node.left->resType, node.right->resType)) {
                        const auto& op_trait = this->get_lang_item_path(node.span(), item_name);
                        check_trait_bound(node.span(), op_trait, {node.right->resType}, node.left->resType);
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
                    switch (node.op) {
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
                    if (!typeck::primitive_operator_has_builtin(operator_kind, node.left->resType, node.right->resType)) {
                        const auto& op_trait = this->get_lang_item_path(node.span(), item_name);
                        check_associated_type(node.span(), node.resType, op_trait, {node.right->resType}, node.left->resType, "Output");
                    }
                    break;
                }
            }

            node.left->visit(*this);
            node.right->visit(*this);
        }

        void visit(::HIR::ExprNodeUniOp& node) override {
            TRACE_FUNCTION_F(&node << " " << ::HIR::ExprNodeUniOp::opname(node.op) << "...");
            auto operator_kind = typeck::PrimitiveOperator::None;
            switch (node.op) {
                case ::HIR::ExprNodeUniOp::Op::Invert:
                    operator_kind = typeck::PrimitiveOperator::Not;
                    if (!typeck::primitive_operator_has_builtin(operator_kind, node.mValue->resType)) {
                        check_associated_type(node.span(), node.resType, this->get_lang_item_path(node.span(), "not"), {}, node.mValue->resType, "Output");
                    }
                    break;
                case ::HIR::ExprNodeUniOp::Op::Negate:
                    operator_kind = typeck::PrimitiveOperator::Neg;
                    if (!typeck::primitive_operator_has_builtin(operator_kind, node.mValue->resType)) {
                        check_associated_type(node.span(), node.resType, this->get_lang_item_path(node.span(), "neg"), {}, node.mValue->resType, "Output");
                    }
                    break;
            }
            node.mValue->visit(*this);
        }

        void visit(::HIR::ExprNodeBorrow& node) override {
            TRACE_FUNCTION_F(&node << " &_ ...");
            check_types_equal(node.span(), node.resType, mResolve.crate.types.borrow(node.mType, node.mValue->resType));
            node.mValue->visit(*this);
        }

        void visit(::HIR::ExprNodeRawBorrow& node) override {
            TRACE_FUNCTION_F(&node << " &raw _ ...");
            check_types_equal(node.span(), node.resType, mResolve.crate.types.pointer(node.mType, node.mValue->resType));
            node.mValue->visit(*this);
        }

        void visit(::HIR::ExprNodeIndex& node) override {
            TRACE_FUNCTION_F(&node << " ... [ ... ]");
            check_associated_type(node.span(), node.resType, mLangIndex, {node.index->resType}, node.mValue->resType, "Output");

            node.mValue->visit(*this);
            node.index->visit(*this);
        }

        void visit(::HIR::ExprNodeCast& node) override {
            TRACE_FUNCTION_F(&node << " " << node.mValue->resType << " as " << node.dstType);
            const Span& sp = node.span();
            DEBUG("Cast res type " << node.resType);
            //ASSERT_BUG(node.span(), node.m_res_type == node.m_dst_type, node.m_res_type << " != " << node.m_dst_type);

            const auto& src_ty = node.mValue->resType;
            const auto& dst_ty = node.resType;

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
                            if (de.inner == mResolve.crate.types.unit() || de.inner == ::HIR::CoreType::U8 || de.inner == ::HIR::CoreType::I8) {
                            } else if (mResolve.type_is_sized(sp, de.inner)) {
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
                            if (se.mAbi != de.mAbi) {
                                ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty << " - different ABI");
                            }
                            if (se.mRettype != de.mRettype) {
                                ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty << " - return type different");
                            }
                            if (se.argTypes.size() != de.argTypes.size()) {
                                ERROR(sp, E0000, "Invalid cast to " << dst_ty << " from " << src_ty << " - argument count different");
                            }
                            for (size_t i = 0; i < se.argTypes.size(); i++) {
                                if (se.argTypes[i] != de.argTypes[i]) {
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

            node.mValue->visit( *this );
        }

        void visit(::HIR::ExprNodeUnsize& node) override {
            TRACE_FUNCTION_F(&node << " ... : " << node.resType);
            const Span& sp = node.span();

            const auto& src_ty = node.mValue->resType;
            const auto& dst_ty = node.resType;

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

                const auto& langUnsize = mResolve.crate.get_lang_item_path_opt("unsize");
                if (!langUnsize.components().empty()) {
                    // _ == < `src_ty` as Unsize< `dst_ty` >::""
                    check_trait_bound(sp, langUnsize, {dst_ty}, src_ty);
                } else if (!mResolve.can_unsize(sp, dst_ty, src_ty)) {
                    ERROR(sp, E0000, "Invalid unsizing operation to " << dst_ty << " from " << src_ty);
                }
            } else if (src_ty->is_Borrow() || dst_ty->is_Borrow()) {
                ERROR(sp, E0000, "Invalid unsizing operation to " << dst_ty << " from " << src_ty);
            } else {
                const auto& langCoerceUnsized = this->get_lang_item_path(node.span(), "coerce_unsized");
                // _ == < `src_ty` as CoerceUnsized< `dst_ty` >::""
                check_trait_bound(sp, langCoerceUnsized, {dst_ty}, src_ty);
            }

            node.mValue->visit(*this);
        }

        void visit(::HIR::ExprNodeDeref& node) override {
            TRACE_FUNCTION_F(&node << " *...");
            const auto& ty = node.mValue->resType;

            const bool builtin = node.traitUsed == ::HIR::ExprNodeDeref::TraitUsed::Builtin
                || (node.traitUsed == ::HIR::ExprNodeDeref::TraitUsed::Unknown
                    && typeck::primitive_operator_has_builtin(typeck::PrimitiveOperator::Deref, ty));
            if (builtin && ty->is_Pointer()) {
                check_types_equal(node.span(), node.resType, ty->as_Pointer().inner);
            } else if (builtin && ty->is_Borrow()) {
                check_types_equal(node.span(), node.resType, ty->as_Borrow().inner);
            } else {
                check_associated_type(node.span(), node.resType, this->get_lang_item_path(node.span(), "deref"), {}, node.mValue->resType, "Target");
            }

            node.mValue->visit(*this);
        }

        void visit(::HIR::ExprNodeEmplace& node) override {
            switch (node.mType) {
                case ::HIR::ExprNodeEmplace::Type::Noop:
                    assert(!node.place);

                    check_types_equal(node.span(), node.resType, node.mValue->resType);
                    break;
                case ::HIR::ExprNodeEmplace::Type::Boxer:
                    // TODO: Check trait and associated type
                    break;
                case ::HIR::ExprNodeEmplace::Type::Placer:
                    // TODO: Check trait
                    break;
            }

            if (node.place) {
                this->visit_node_ptr(node.place);
            }
            this->visit_node_ptr(node.mValue);
        }

        void visit(::HIR::ExprNodeTupleVariant& node) override {
            TRACE_FUNCTION_F(&node << " " << node.mPath << "(...,) [" << (node.isStruct ? "struct" : "enum") << "]");
            const auto& sp = node.span();

            // - Create ivars in path, and set result type
            const auto& ty = node.resType;

            const ::HIR::t_tuple_fields* fields_ptr = nullptr;
            ASSERT_BUG(sp, ty->is_Path(), "Result type of _TupleVariant isn't Path");
            TU_MATCH(::HIR::TypePathBinding, (ty->as_Path().binding), (e), (Unbound, BUG(sp, "Unbound type in _TupleVariant - " << ty);), (Opaque, BUG(sp, "Opaque type binding in _TupleVariant - " << ty);), (Enum, const auto& var_name = node.mPath.mPath.components().back(); const auto& enm = *e; size_t idx = enm.find_variant(var_name); const auto& var_ty = enm.mData.as_Data()[idx].type; const auto& str = *var_ty->as_Path().binding.as_Struct(); ASSERT_BUG(sp, str.mData.is_Tuple(), "Pointed variant of TupleVariant (" << node.mPath << ") isn't a Tuple"); fields_ptr = &str.mData.as_Tuple();), (Union, BUG(sp, "Union in TupleVariant");), (ExternType, BUG(sp, "ExternType in TupleVariant");), (Struct, ASSERT_BUG(sp, e->mData.is_Tuple(), "Pointed struct in TupleVariant (" << node.mPath << ") isn't a Tuple"); fields_ptr = &e->mData.as_Tuple();))
            assert(fields_ptr);
            const ::HIR::t_tuple_fields& fields = *fields_ptr;
            ASSERT_BUG(sp, fields.size() == node.mArgs.size(), "");

            // Bind fields with type params (coercable)
            // TODO: Remove use of m_arg_types (maybe assert that cache is correct?)
            for (unsigned int i = 0; i < node.mArgs.size(); i++) {
                const auto& des_ty_r = fields[i].ent;
                const auto* des_ty = &des_ty_r;
                if (monomorphise_type_needed(des_ty_r)) {
                    assert(node.argTypes[i] != ::HIR::TypeRef());
                    des_ty = &node.argTypes[i];
                }

                check_types_equal(*des_ty, node.mArgs[i]);
            }

            for (auto& val : node.mArgs) {
                val->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeStructLiteral& node) override {
            TRACE_FUNCTION_F(&node << " " << node.realPath << "{...} [" << (node.isStruct ? "struct" : "enum") << "]");
            const auto& sp = node.span();
            if (node.baseValue) {
                check_types_equal(node.baseValue->span(), node.resType, node.baseValue->resType);
            }
            const auto& ty_path = node.realPath;

            // - Create ivars in path, and set result type
            const auto& ty = node.resType;
            ASSERT_BUG(sp, ty->is_Path(), "Result type of _StructLiteral isn't Path");

            const ::HIR::t_struct_fields* fields_ptr = nullptr;
            TU_MATCH_HDRA( (ty->as_Path().binding), {)
            TU_ARMA(Unbound, e) {
                }
                TU_ARMA(Opaque, e) {
                }
                TU_ARMA(Enum, e) {
                    const auto& var_name = ty_path.mPath.components().back();
                    const auto& enm = *e;
                    auto idx = enm.find_variant(var_name);
                    ASSERT_BUG(sp, idx != SIZE_MAX, "");
                    ASSERT_BUG(sp, enm.mData.is_Data(), "");
                    const auto& var = enm.mData.as_Data()[idx];

                    const auto& str = *var.type->as_Path().binding.as_Struct();
                    ASSERT_BUG(sp, var.is_struct, "Struct literal for enum on non-struct variant");
                    fields_ptr = &str.mData.as_Named();
                }
                TU_ARMA(Union, e) {
                    fields_ptr = &e->mVariants;
                    ASSERT_BUG(node.span(), node.values.size() > 0, "Union with no values");
                    ASSERT_BUG(node.span(), node.values.size() == 1, "Union with multiple values");
                    ASSERT_BUG(node.span(), !node.baseValue, "Union can't have a base value");
                }
                TU_ARMA(ExternType, e) {
                    BUG(sp, "ExternType in StructLiteral");
                }
                TU_ARMA(Struct, e) {
                    if (e->mData.is_Unit()) {
                        ASSERT_BUG(node.span(), node.values.size() == 0, "Values provided for unit-like struct");
                        ASSERT_BUG(node.span(), !node.baseValue, "Values provided for unit-like struct");
                        return;
                    }

                    ASSERT_BUG(node.span(), e->mData.is_Named(), "StructLiteral not pointing to a braced struct, instead " << e->mData.tag_str() << " - " << ty);
                    fields_ptr = &e->mData.as_Named();
                }
            }
            ASSERT_BUG(node.span(), fields_ptr, "Didn't get field for path in _StructLiteral - " << ty);
            const ::HIR::t_struct_fields& fields = *fields_ptr;
            for(const auto& fld : fields) {
                DEBUG(fld.name << ": " << fld.ty);
            }

            auto ms = MonomorphStatePtr(mResolve.crate.types, ty, &ty_path.mParams, nullptr);

            // Bind fields with type params (coercable)
            for( auto& val : node.values)
            {
                const auto& name = val.first;
                auto it = ::std::find_if(fields.begin(), fields.end(), [&](const HIR::StructField& v) -> bool {
                    return v.name == name;
                });
                assert(it != fields.end());
                const auto& des_ty_r = it->ty;
                auto& des_ty_cache = node.valueTypes[it - fields.begin()];
                const auto* des_ty = &des_ty_r;

                DEBUG(name << " : " << des_ty_r);
                if (monomorphise_type_needed(des_ty_r)) {
                    ASSERT_BUG(node.span(), des_ty_cache != ::HIR::TypeRef(), "Type " << des_ty_r << " needs monomorph, but isn't in cache: Field " << name);
                    des_ty_cache = ms.monomorph_type(node.span(), des_ty_r);
                    mResolve.expand_associated_types(node.span(), des_ty_cache);
                    des_ty = &des_ty_cache;
                }
                DEBUG("." << name << " : " << *des_ty);
                check_types_equal(*des_ty, val.second);
            }

            for( auto& val : node.values ) {
                val.second->visit(*this);
            }
            if( node.baseValue ) {
                node.baseValue->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeUnitVariant& node) override {
            TRACE_FUNCTION_F(&node << " " << node.mPath << " [" << (node.isStruct ? "struct" : "enum") << "]");
            const auto& sp = node.span();
            const auto& ty = node.resType;
            ASSERT_BUG(sp, ty->is_Path(), "Result type of _UnitVariant isn't Path");

            TU_MATCH(
                ::HIR::TypePathBinding,
                (ty->as_Path().binding),
                (e),
                (Unbound, ),
                (Opaque, ),
                (
                    Enum, const auto& var_name = node.mPath.mPath.components().back(); const auto& enm = *e; if (const auto* e = enm.mData.opt_Data()) {
                        auto idx = enm.find_variant(var_name);
                        ASSERT_BUG(sp, idx != SIZE_MAX, "");
                        ASSERT_BUG(sp, (*e)[idx].type == mResolve.crate.types.unit(), "");
                    }
                ),
                (Union, BUG(sp, "Union with _UnitVariant");),
                (ExternType, BUG(sp, "ExternType with _UnitVariant");),
                (Struct, assert(e->mData.is_Unit());)
            )
        }

        void check_function(const Span& sp, const ::HIR::Path& path, HIR::ExprCallCache& cache) {
            // Do function resolution again, this time with concrete types.
            const ::HIR::Function* fcn_ptr = nullptr;
            MonomorphStatePtr monomorph_cb(mResolve.crate.types);

            TU_MATCH_HDRA( (path.mData), {)
            TU_ARMA(Generic, e) {
                    const auto& path_params = e.mParams;

                    const auto& fcn = mResolve.crate.get_function_by_path(sp, e.mPath);
                    fcn_ptr = &fcn;
                    cache.fcnParams = &fcn.mParams;

                    monomorph_cb = MonomorphStatePtr(mResolve.crate.types, nullptr, nullptr, &path_params);
                }
                TU_ARMA(UfcsKnown, e) {
                    const auto& trait_params = e.trait.mParams;
                    const auto& path_params = e.params;

                    const auto& trait = mResolve.crate.get_trait_by_path(sp, e.trait.mPath);
                    if (trait.values.count(e.item) == 0) {
                        BUG(sp, "Method '" << e.item << "' of trait " << e.trait.mPath << " doesn't exist");
                    }

                    const auto& fcn = trait.values.at(e.item).as_Function();
                    cache.fcnParams = &fcn.mParams;
                    cache.topParams = &trait.mParams;

                    // Add a bound requiring the Self type impl the trait
                    check_trait_bound(sp, e.trait.mPath, e.trait.mParams, e.type);

                    fcn_ptr = &fcn;

                    monomorph_cb = MonomorphStatePtr(mResolve.crate.types, e.type, &trait_params, &path_params);
                }
                TU_ARMA(UfcsUnknown, e) {
                    TODO(sp, "Hit a UfcsUnknown (" << path << ") - Is this an error?");
                }
                TU_ARMA(UfcsInherent, e) {
                    // - Locate function (and impl block)
                    const ::HIR::TypeImpl* impl_ptr = nullptr;
                    mResolve.crate.find_type_impls(e.type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                        DEBUG("- impl" << impl.mParams.fmt_args() << " " << impl.mType);
                        auto it = impl.methods.find(e.item);
                        if (it == impl.methods.end()) {
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

                    cache.fcnParams = &fcn_ptr->mParams;

                    // NOTE: Trusts the existing cache.
                    ASSERT_BUG(sp, e.impl_params.types.size() == impl_ptr->mParams.types.size(), "Path impl_params cache is missized - " << e.impl_params.types.size() << " != " << impl_ptr->mParams.types.size());
                    auto& impl_params = e.impl_params;

                    // Create monomorphise callback
                    const auto& fcn_params = e.params;
                    monomorph_cb = MonomorphStatePtr(mResolve.crate.types, e.type, &impl_params, &fcn_params);
                }
            }

            assert( fcn_ptr );
            const auto& fcn = *fcn_ptr;
            monomorph_cb.set_consteval_state(mResolve.crate, HIR::ItemPath(path));

            // --- Monomorphise the argument/return types (into current context)
            cache.argTypes.clear();
            for(const auto& arg : fcn.mArgs) {
                DEBUG("Arg " << arg.first << ": " << arg.second);
                cache.argTypes.push_back(monomorph_cb.monomorph_type(sp, arg.second, false));
                mResolve.expand_associated_types(sp, cache.argTypes.back());
                DEBUG("= " << cache.argTypes.back());
            }
            DEBUG("Ret " << fcn.returnType);
            // Replace ErasedType and monomorphise
            cache.argTypes.push_back( monomorph_cb.monomorph_type(sp, fcn.returnType, false) );
            rewrite_ty_with(mResolve.crate.types, cache.argTypes.back(), [&](HIR::TypeRef& ty, HIR::TypeData&)->bool {
                if (this->expand_erased_types && ty->is_ErasedType() && ty->as_ErasedType().inner.is_Fcn()) {
                    const auto& e = ty->as_ErasedType().inner.as_Fcn();

                    // Check the origin, because monomorph might end up introducing other erased types
                    if (e.origin == path) {
                        ASSERT_BUG(sp, e.index < fcn_ptr->mCode.erasedTypes.size(), "");
                        const auto& erased_type_replacement = fcn_ptr->mCode.erasedTypes.at(e.index);
                        ty = monomorph_cb.monomorph_type(sp, erased_type_replacement, false);
                        return true;
                    }
                }
                return false;
                });
            mResolve.expand_associated_types(sp, cache.argTypes.back());
            DEBUG("= " << cache.argTypes.back());

            cache.monomorph.reset( new MonomorphStatePtr(monomorph_cb) );

            // Bounds
            for(size_t i = 0; i < cache.fcnParams->types.size(); i ++)
            {
            }
            for(const auto& bound : cache.fcnParams->bounds)
            {
                TU_MATCH_HDRA( (bound), {)
                TU_ARMA(Lifetime, be) {
                    }
                    TU_ARMA(TypeLifetime, be) {
                    }
                    TU_ARMA(TraitBound, be) {
                        HIR::GenericParams empty_hrtb;
                        auto _ = cache.monomorph->push_hrb(be.hrtbs ? *be.hrtbs : empty_hrtb);
                        DEBUG("Bound " << be.type << ":  " << be.trait);
                        auto real_type = cache.monomorph->monomorph_type(sp, be.type);
                        mResolve.expand_associated_types(sp, real_type);
                        auto real_trait = cache.monomorph->monomorph_traitpath(sp, be.trait, false);
                        mResolve.expand_associated_types_tp(sp, real_trait);
                        DEBUG("= (" << real_type << ": " << real_trait << ")");
                        const auto& trait_params = real_trait.mPath.mParams;

                        const auto& trait_path = be.trait.mPath.mPath;
                        check_trait_bound(sp, trait_path, trait_params, real_type);

                        // TODO: Either - Don't include the above impl bound, or change the below trait to the one that has that type
                        for (auto& assoc : real_trait.typeBounds) {
                            ::HIR::GenericPath type_trait_path;
                            bool has_ty = mResolve.trait_contains_type(sp, real_trait.mPath, *be.trait.traitPtr, assoc.first.c_str(), type_trait_path);
                            ASSERT_BUG(sp, has_ty, "Type " << assoc.first << " not found in chain of " << real_trait.mPath);

                            check_associated_type(sp, assoc.second.type, type_trait_path.mPath, type_trait_path.mParams, real_type, assoc.first.c_str());
                        }
                    }
                    TU_ARMA(TypeEquality, be) {
                        auto real_type_left = cache.monomorph->monomorph_type(sp, be.type);
                        auto real_type_right = cache.monomorph->monomorph_type(sp, be.other_type);
                        mResolve.expand_associated_types(sp, real_type_left);
                        mResolve.expand_associated_types(sp, real_type_right);
                        check_types_equal(sp, real_type_left, real_type_right);
                    }
                }
            }
        }

        void visit(::HIR::ExprNodeCallPath& node) override {
            const auto& sp = node.span();
            TRACE_FUNCTION_F(&node << " " << node.mPath << "(..., )");

            for (auto& val : node.mArgs) {
                val->visit(*this);
            }

            check_function(sp, node.mPath, node.cache);

            // Check types
            for (unsigned int i = 0; i < node.cache.argTypes.size() - 1; i++) {
                DEBUG("CHECK ARG " << i << " " << node.cache.argTypes[i] << " == " << node.mArgs[i]->resType);
                check_types_equal(sp, node.cache.argTypes[i], node.mArgs[i]->resType);
            }
            for (unsigned int i = node.cache.argTypes.size() - 1; i < node.mArgs.size(); i++) {
                DEBUG("CHECK ARG " << i << " *  == " << node.mArgs[i]->resType);
                // TODO: Check that the types here are valid.
            }
            DEBUG("CHECK RV " << node.resType << " == " << node.cache.argTypes.back());
            check_types_equal(sp, node.resType, node.cache.argTypes.back());
        }

        void visit(::HIR::ExprNodeCallValue& node) override {
            TRACE_FUNCTION_F(&node << " (...)(..., )");

            const auto& val_ty = node.mValue->resType;

            if (val_ty->is_Function() || val_ty->is_NamedFunction()) {
                DEBUG("- Function pointer: " << val_ty);
                ::HIR::TypeRef tmp_ft;
                const auto* e = val_ty->opt_Function();
                if (!e) {
                    tmp_ft = mResolve.crate.types.function(val_ty->as_NamedFunction().decay(mResolve.crate.types, node.span()));
                    mResolve.expand_associated_types(node.span(), tmp_ft);
                    e = &tmp_ft->as_Function();
                }
                auto hrls = e->hrls.make_empty_params(true);
                auto m = MonomorphHrlsOnly(mResolve.crate.types, hrls);
                if (e->is_variadic ? node.mArgs.size() < e->argTypes.size() : node.mArgs.size() != e->argTypes.size()) {
                    ERROR(node.span(), E0000, "Incorrect number of arguments to call via " << val_ty);
                }
                for (unsigned int i = 0; i < e->argTypes.size(); i++) {
                    check_types_equal(node.mArgs[i]->span(), m.monomorph_type(node.span(), e->argTypes[i]), node.mArgs[i]->resType);
                }
                check_types_equal(node.span(), node.resType, m.monomorph_type(node.span(), e->mRettype));
            } else if (node.traitUsed == ::HIR::ExprNodeCallValue::TraitUsed::Unknown) {
            } else {
                // 1. Look up the encoded trait
                const ::HIR::SimplePath* trait_p;
                switch (node.traitUsed) {
                    case ::HIR::ExprNodeCallValue::TraitUsed::Fn:
                        trait_p = &mResolve.crate.get_lang_item_path(node.span(), "fn");
                        break;
                    case ::HIR::ExprNodeCallValue::TraitUsed::FnMut:
                        trait_p = &mResolve.crate.get_lang_item_path(node.span(), "fn_mut");
                        break;
                    case ::HIR::ExprNodeCallValue::TraitUsed::FnOnce:
                        trait_p = &mResolve.crate.get_lang_item_path(node.span(), "fn_once");
                        break;
                    default:
                        throw "";
                }
                const auto& trait = *trait_p;

                ::std::vector<::HIR::TypeRef> tup_ents;
                for (const auto& arg : node.mArgs) {
                    tup_ents.push_back(arg->resType);
                }
                ::HIR::PathParams params;
                params.types.push_back(mResolve.crate.types.tuple(mv$(tup_ents)));

                bool found = mResolve.find_impl(node.span(), trait, &params, val_ty, [&](auto, bool fuzzy) -> bool {
                    ASSERT_BUG(node.span(), !fuzzy, "Fuzzy match in check pass");
                    return true;
                });
                if (!found) {
                    ERROR(node.span(), E0000, "Unable to find a matching impl of " << trait << " for " << val_ty);
                }
                auto exp_ret = mResolve.crate.types.path(::HIR::Path(node.mValue->resType, {mResolve.crate.get_lang_item_path(node.span(), "fn_once"), mv$(params)}, "Output", {}), {});
                mResolve.expand_associated_types(node.span(), exp_ret);
                check_types_equal(node.span(), node.resType, exp_ret);
            }

            node.mValue->visit(*this);
            for (auto& val : node.mArgs) {
                val->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeCallMethod& node) override {
            TRACE_FUNCTION_F(&node << " (...)." << node.method << "(...,) - " << node.methodPath);

            node.mValue->visit(*this);
            for (auto& val : node.mArgs) {
                val->visit(*this);
            }

            const Span& sp = node.span();
            check_function(sp, node.methodPath, node.cache);

            // Check types
            for (unsigned int i = 0; i < node.cache.argTypes.size() - 2; i++) {
                DEBUG("CHECK ARG " << i << " " << node.cache.argTypes[1 + i] << " == " << node.mArgs[i]->resType);
                check_types_equal(sp, node.cache.argTypes[1 + i], node.mArgs[i]->resType);
            }
            for (unsigned int i = node.cache.argTypes.size() - 1; i < node.mArgs.size(); i++) {
                DEBUG("CHECK ARG " << i << " *  == " << node.mArgs[i]->resType);
                // TODO: Check that the types here are valid.
            }
            DEBUG("CHECK RV " << node.resType << " == " << node.cache.argTypes.back());
            check_types_equal(sp, node.resType, node.cache.argTypes.back());
        }

        void visit(::HIR::ExprNodeField& node) override {
            TRACE_FUNCTION_F(&node << " (...)." << node.field);
            const auto& sp = node.span();
            const auto& str_ty = node.mValue->resType;

            bool is_index = ('0' <= node.field.c_str()[0] && node.field.c_str()[0] <= '9');
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

            node.mValue->visit(*this);
        }

        void visit(::HIR::ExprNodeTuple& node) override {
            TRACE_FUNCTION_F(&node << " (...,)");
            ASSERT_BUG(node.span(), node.resType->is_Tuple(), "Tuple literal didn't return tuple");
            const auto& tys = node.resType->as_Tuple();

            ASSERT_BUG(node.span(), tys.size() == node.vals.size(), "Bad element count in tuple literal - " << tys.size() << " != " << node.vals.size());
            for (unsigned int i = 0; i < node.vals.size(); i++) {
                check_types_equal(node.span(), tys[i], node.vals[i]->resType);
            }

            for (auto& val : node.vals) {
                val->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeArrayList& node) override {
            TRACE_FUNCTION_F(&node << " [...,]");
            // Cleanly equate into array (with coercions)
            const auto& inner_ty = node.resType->as_Array().inner;
            for (auto& val : node.vals) {
                check_types_equal(val->span(), inner_ty, val->resType);
            }

            for (auto& val : node.vals) {
                val->visit(*this);
            }
        }

        void visit(::HIR::ExprNodeArraySized& node) override {
            TRACE_FUNCTION_F(&node << " [...; " << node.mSize << "]");

            //check_types_equal(node.m_size->span(), ::HIR::TypeRef(::HIR::Primitive::Usize), node.m_size->m_res_type);
            const auto& inner_ty = node.resType->as_Array().inner;
            check_types_equal(node.val->span(), inner_ty, node.val->resType);

            node.val->visit(*this);
            //if(node.m_size.is_Unevaluated() && node.m_size.as_Unevaluated().is_Unevaluated())
            //{
            //    (*node.m_size.as_Unevaluated().as_Unevaluated())->visit( *this );
            //}
        }

        void visit(::HIR::ExprNodeLiteral& node) override {
            // No validation needed
        }

        void visit(::HIR::ExprNodePathValue& node) override {
            TRACE_FUNCTION_F(&node << " " << node.mPath);
            const Span& sp = node.span();

            MonomorphState out_params(mResolve.crate.types);
            StaticTraitResolve::ValuePtr v = this->mResolve.get_value(sp, node.mPath, out_params, /*signature_only=*/true);
            HIR::TypeRef ty;
            TU_MATCH_HDRA( (v), {)
            TU_ARMA(NotFound, ve) {
                    BUG(sp, node.mPath << " Not found");
                }
                TU_ARMA(NotYetKnown, ve) {
                    // If the exact value can't be found, then
                    BUG(sp, node.mPath << " still unknown (has ivars?)");
                }
                TU_ARMA(Static, ve) {
                    ty = out_params.monomorph_type(node.span(), ve->mType);
                    this->mResolve.expand_associated_types(sp, ty);
                }
                TU_ARMA(Constant, ve) {
                    ty = out_params.monomorph_type(node.span(), ve->mType);
                    this->mResolve.expand_associated_types(sp, ty);
                }
                TU_ARMA(StructConstant, ve) {
                    // TODO: Check struct type
                }
                TU_ARMA(EnumValue, ve) {
                    // TODO: Check enum variant type
                }

                TU_ARMA(Function, ve) {
                    ty = mResolve.crate.types.intern(::HIR::TypeData::make_NamedFunction({node.mPath.clone(), ve}));
                }
                TU_ARMA(StructConstructor, ve) {
                    ty = mResolve.crate.types.intern(::HIR::TypeData::make_NamedFunction({node.mPath.clone(), ve.s}));
                }
                TU_ARMA(EnumConstructor, ve) {
                    ty = mResolve.crate.types.intern(::HIR::TypeData::make_NamedFunction({node.mPath.clone(), ::HIR::TypeDataNamedFunctionTy::make_EnumConstructor({ve.e, ve.v})}));
                }
            }
            if( ty != HIR::TypeRef() ) {
                check_types_equal(sp, node.resType, ty);
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

            if (node.mCode) {
                check_types_equal(node.mCode->span(), node.returnType, node.mCode->resType);

                auto loops = ::std::move(this->loops);

                this->closure_ret_types.push_back(RetTarget(node.returnType));
                node.mCode->visit(*this);
                this->closure_ret_types.pop_back();

                this->loops = ::std::move(loops);
            }
        }

        void visit(::HIR::ExprNodeGenerator& node) override {
            TRACE_FUNCTION_F(&node << " /*gen*/ |...| ...");

            if (node.mCode) {
                auto loops = ::std::move(this->loops);

                check_types_equal(node.mCode->span(), node.returnType, node.mCode->resType);
                this->closure_ret_types.push_back(RetTarget(node.returnType, node.yieldTy));
                node.mCode->visit(*this);
                this->closure_ret_types.pop_back();

                this->loops = ::std::move(loops);
            }
        }

        void visit(::HIR::ExprNodeGeneratorWrapper& node) override {
            TRACE_FUNCTION_F(&node << " /*gen w*/ |...| ...");

            if (node.mCode) {
                auto loops = ::std::move(this->loops);

                check_types_equal(node.mCode->span(), node.returnType, node.mCode->resType);
                this->closure_ret_types.push_back(RetTarget(node.returnType, node.yieldTy));
                node.mCode->visit(*this);
                this->closure_ret_types.pop_back();

                this->loops = ::std::move(loops);
            }
        }

        void visit(::HIR::ExprNodeAsyncBlock& node) override {
            TRACE_FUNCTION_F(&node << " async { ... }");

            // Can be null after generation
            if (node.mCode) {
                auto loops = ::std::move(this->loops);
                this->closure_ret_types.push_back(RetTarget(node.mCode->resType));
                node.mCode->visit(*this);
                this->closure_ret_types.pop_back();
                this->loops = ::std::move(loops);
            }
        }

    private:
        void check_types_equal(const ::HIR::TypeData* l, const ::HIR::ExprNodeP& node) const {
            check_types_equal(node->span(), l, node->resType);
        }

        void check_types_equal(const Span& sp, const ::HIR::TypeData* l, const ::HIR::TypeData* r) const {
            struct Resolve: HIR::ResolvePlaceholders {
                HIR::TypeInterner& types;
                mutable ::HIR::TypeRef tmp;

                explicit Resolve(HIR::TypeInterner& types): types(types) {}

                const ::HIR::TypeData* get_type(const Span& sp, const HIR::TypeData* ty) const override {
                    //ASSERT_BUG(sp, ty->is_Infer(), "Unexpected ivar");
                    if (const auto* e = ty->opt_ErasedType()) {
                        if (const auto* ee = e->inner.opt_Alias()) {
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
            } get_types(mResolve.crate.types);

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
            MonomorphHrlsOnly(mResolve.crate.types, HIR::PathParams()).monomorph_type(sp, l);
            MonomorphHrlsOnly(mResolve.crate.types, HIR::PathParams()).monomorph_type(sp, r);
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
            mResolve.expand_associated_types(sp, normalized_type);
            for (auto& type : normalized_params.types) {
                mResolve.expand_associated_types(sp, type);
            }
            const bool found = mResolve.find_impl(
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
            bool found = mResolve.find_impl(sp, trait, &params, ity, [&](auto impl, bool fuzzy) {
                auto atyv = impl.get_type(mResolve.crate.types, name, {});
                if (atyv == ::HIR::TypeRef()) {
                    // TODO: Check that `res` is <ity as trait>::name
                } else {
                    mResolve.expand_associated_types(sp, atyv);
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
            for (size_t i = 0; i < pat.implicitDerefCount; i++) {
                typ = typ->as_Borrow().inner;
            }
            const ::HIR::TypeData* ty = typ;

            TU_MATCH_HDRA( (pat.mData), { )
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
                        check_types_equal(sp, ty, mResolve.crate.types.primitive(e.type));
                    }
                }
                TU_ARMA(Float, e) {
                    if (e.type == ::HIR::CoreType::Str) {
                    } else {
                        check_types_equal(sp, ty, mResolve.crate.types.primitive(e.type));
                    }
                }
                TU_ARMA(String, e) {
                    check_types_equal(sp, ty, mResolve.crate.types.borrow(::HIR::BorrowType::Shared, mResolve.crate.types.primitive(::HIR::CoreType::Str)));
                }
                TU_ARMA(ByteString, e) {
                    // Can either be a slice or an array
                    //check_types_equal(sp, ty, ::HIR::TypeRef::new_borrow(::HIR::BorrowType::Shared, ::HIR::TypeRef::new_slice(::HIR::CoreType::U8)));
                }
                TU_ARMA(Named, e) {
                    MonomorphState ms(mResolve.crate.types);
                    auto v = mResolve.get_value(sp, e.path, ms, /*signature_only*/ true);
                    if (!v.is_Constant()) {
                        BUG(sp, "Pattern::Value::Named not a const - " << e.path);
                    }
                    HIR::TypeRef tmp;
                    const auto& const_ty = ms.maybe_monomorph_type(sp, tmp, v.as_Constant()->mType);
                    check_types_equal(sp, ty, const_ty);
                }
            }
        }

        const ::HIR::SimplePath& get_lang_item_path(const Span& sp, const char* name) const {
            return mResolve.crate.get_lang_item_path(sp, name);
        }
    };

    class OuterVisitor: public ::HIR::Visitor {
        StaticTraitResolve mResolve;

    public:
        OuterVisitor(const ::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.types)
            , mResolve(crate)
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
                        auto ty_usize = mResolve.crate.types.primitive(::HIR::CoreType::Usize);
                        ExprVisitorValidate ev(mResolve, tmp, ty_usize);
                        ev.visit_root(*(*se)->expr);
                    }
                }
                ty = mResolve.crate.types.intern(std::move(data));
            } else {
                ::HIR::Visitor::visit_type(ty);
            }
        }

        void visit_constgeneric(::HIR::ConstGeneric& value) override {
            if (auto* unevaluated = value.opt_Unevaluated()) {
                t_args tmp;
                auto& expr = *(**unevaluated).expr;
                ExprVisitorValidate ev(mResolve, tmp, expr->resType);
                ev.visit_root(expr);
            }
        }

        // ------
        // Code-containing items
        // ------
        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            auto _ = this->mResolve.set_item_generics(item.mParams);
            if (item.mCode) {
                DEBUG("Function code " << p);
                ::HIR::TypeRef tmp;
                const auto& ret_ty = mResolve.fix_trait_default_return(item.mCode->span(), p, item.returnType, tmp);
                ExprVisitorValidate ev(mResolve, item.mArgs, ret_ty);
                ev.visit_root(item.mCode);
            } else {
                DEBUG("Function code " << p << " (none)");
            }
        }

        void visit_static(::HIR::ItemPath p, ::HIR::Static& item) override {
            auto _ = this->mResolve.set_item_generics(item.mParams);
            if (item.mValue) {
                t_args tmp;
                ExprVisitorValidate ev(mResolve, tmp, item.mType);
                ev.visit_root(item.mValue);
            }
        }

        void visit_constant(::HIR::ItemPath p, ::HIR::Constant& item) override {
            auto _ = this->mResolve.set_item_generics(item.mParams);
            if (item.mValue) {
                t_args tmp;
                ExprVisitorValidate ev(mResolve, tmp, item.mType);
                ev.visit_root(item.mValue);
            }
            mResolve.expand_associated_types(Span(), item.mType);
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = this->mResolve.set_impl_generics(MetadataType::None, item.mParams);

            ::HIR::TypeRef enum_type = mResolve.crate.types.primitive(::HIR::Enum::get_repr_type(item.tagRepr));
            if (auto* e = item.mData.opt_Value()) {
                for (auto& var : e->variants) {
                    DEBUG("Enum value " << p << " - " << var.name);

                    if (var.expr) {
                        t_args tmp;
                        ExprVisitorValidate ev(mResolve, tmp, enum_type);
                        ev.visit_root(var.expr);
                    }
                }
            }
        }

        void visit_trait(::HIR::ItemPath p, ::HIR::Trait& item) override {
            auto _ = this->mResolve.set_impl_generics(MetadataType::TraitObject, item.mParams);
            ::HIR::Visitor::visit_trait(p, item);
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType);
            auto _ = this->mResolve.set_impl_generics(impl.mType, impl.mParams);

            ::HIR::Visitor::visit_type_impl(impl);
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            TRACE_FUNCTION_F("impl" << impl.mParams.fmt_args() << " " << trait_path << " for " << impl.mType);
            auto _ = this->mResolve.set_impl_generics(impl.mType, impl.mParams);

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
                return pitem.as_Enum().mParams;
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
                    (Function, return e.mParams;),
                    (Constant, return e.mParams;),
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

                TU_MATCH(::HIR::TypeItem, (item), (e), (Import, BUG(sp, "Type path pointed to import - " << path);), (TypeAlias, BUG(sp, "Type path pointed to type alias - " << path);), (TraitAlias, BUG(sp, "Type path pointed to trait alias - " << path);), (ExternType, static ::HIR::GenericParams empty_params; return empty_params;), (Module, BUG(sp, "Type path pointed to module - " << path);), (Struct, return e.mParams;), (Enum, return e.mParams;), (Union, return e.mParams;), (Trait, return e.mParams;))
            } break;
        }
        throw "";
    }

    class Visitor: public ::HIR::Visitor {
        ::HIR::Crate& crate;
        StaticTraitResolve mResolve;

        const ::HIR::Trait* currentTrait = nullptr;
        const ::HIR::ItemPath* currentTraitPath = nullptr;

        ::HIR::GenericParams* curParams = nullptr;
        unsigned curParamsLevel = 0;
        ::HIR::ItemPath* fcnPath = nullptr;
        ::HIR::Function* fcnPtr = nullptr;
        unsigned int fcnErasedCount = 0;

        ::std::vector<const ::HIR::TypeData*> selfTypes;
        ::std::vector<::HIR::LifetimeRef*> currentLifetime;

        typedef ::std::vector<::std::pair<const ::HIR::SimplePath*, const ::HIR::Trait*>> t_trait_imports;
        t_trait_imports traits;

    public:
        Visitor(::HIR::Crate& crate)
            : ::HIR::Visitor(nullptr, crate.types)
            , crate(crate)
            , mResolve(crate)
        {
        }

    private:
        struct ModTraitsGuard {
            Visitor* v;
            t_trait_imports old_imports;

            ~ModTraitsGuard() {
                this->v->traits = mv$(this->old_imports);
            }
        };

        ModTraitsGuard push_mod_traits(const ::HIR::Module& mod) {
            static Span sp;
            DEBUG("");
            auto rv = ModTraitsGuard{this, mv$(this->traits)};
            for (const auto& trait_path : mod.traits) {
                DEBUG("- " << trait_path);
                traits.push_back(::std::make_pair(&trait_path, &this->crate.get_trait_by_path(sp, trait_path)));
            }
            return rv;
        }

        void check_parameters(const Span& sp, const ::HIR::GenericParams& param_def, ::HIR::PathParams& param_vals) {
            MonomorphStatePtr ms(crate.types, selfTypes.empty() ? nullptr : selfTypes.back(), &param_vals, nullptr);

            if (param_vals.mLifetimes.size() == 0) {
                param_vals.mLifetimes.resize(param_def.mLifetimes.size());
            }
            if (param_vals.mLifetimes.size() != param_def.mLifetimes.size()) {
                ERROR(sp, E0000, "Incorrect lifetime param count, expected " << param_def.mLifetimes.size() << ", got " << param_vals.mLifetimes.size());
            }

            while (param_vals.types.size() < param_def.types.size()) {
                unsigned int i = param_vals.types.size();
                const auto& ty_def = param_def.types[i];
                if (ty_def.defaultValue->is_Infer()) {
                    ERROR(sp, E0000, "Unspecified parameter with no default - " << param_def.fmt_args() << " with " << param_vals);
                }

                // Replace and expand
                param_vals.types.push_back(ms.monomorph_type(sp, ty_def.defaultValue));
                DEBUG("Add missing param (using default): " << param_vals.types.back());
            }

            if (param_vals.types.size() != param_def.types.size()) {
                ERROR(sp, E0000, "Incorrect number of parameters - expected " << param_def.types.size() << ", got " << param_vals.types.size());
            }

            for (unsigned int i = 0; i < param_vals.types.size(); i++) {
                if (param_vals.types[i] == ::HIR::TypeRef()) {
                    // TODO: Why is this pulling in the default? Why not just leave it as-is

                    //if( param_def.m_types[i].m_default == ::HIR::TypeRef() )
                    //    ERROR(sp, E0000, "Unspecified parameter with no default");
                    // TODO: Monomorphise?
                    param_vals.types[i] = ms.monomorph_type(sp, param_def.types[i].defaultValue);
                    DEBUG("Update `_` param (using default): " << param_def.types[i].defaultValue << " -> " << param_vals.types[i]);
                }
            }

            // TODO: Check generic bounds
            for (const auto& bound : param_def.bounds) {
                TU_MATCH(
                    ::HIR::GenericBound,
                    (bound),
                    (e),
                    (Lifetime, ),
                    (TypeLifetime, ),
                    (
                        TraitBound,
                        // TODO: Check for an implementation of this trait
                        DEBUG("TODO: Check bound " << e.type << " : " << e.trait.mPath);
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
                        if (!currentLifetime.empty() && currentLifetime.back()) {
                            lft = *currentLifetime.back();
                        }
                        // Otherwise, try to make a new one
                        else if (curParams) {
                            auto idx = curParams->mLifetimes.size();
                            curParams->mLifetimes.push_back(HIR::LifetimeDef{RcString::new_interned(FMT("elided#" << idx))});
                            lft.binding = curParamsLevel * 256 + idx;
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

            for (auto& lft : pp.mLifetimes) {
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
                currentLifetime.push_back(&e->lifetime);
            }

            auto self = crate.types.self();
            if (data.is_ErasedType()) {
                selfTypes.push_back(self);
            }

            auto saved_params = std::make_pair(curParams, curParamsLevel);
            if (auto* e = data.opt_Function()) {
                curParams = &e->hrls;
                curParamsLevel = 3;
            }

            TU_MATCH_HDRA((data), {)
            TU_ARMA(Infer, e) {}
            TU_ARMA(Diverge, e) {}
            TU_ARMA(Primitive, e) {}
            TU_ARMA(Generic, e) {}
            TU_ARMA(Path, e) this->visit_path(e.path, ::HIR::Visitor::PathContext::TYPE);
            TU_ARMA(TraitObject, e) {
                if (e.mTrait.mPath != ::HIR::SimplePath()) this->visit_trait_path(e.mTrait);
                for (auto& marker : e.markers) this->visit_generic_path(marker, ::HIR::Visitor::PathContext::TYPE);
            }
            TU_ARMA(ErasedType, e) {
                TU_MATCH_HDRA((e.inner), {)
                TU_ARMA(Known, inner) this->visit_type(inner);
                TU_ARMA(Alias, inner) this->visit_path_params(inner.params);
                TU_ARMA(Fcn, inner) if (inner.origin != ::HIR::SimplePath()) this->visit_path(inner.origin, ::HIR::Visitor::PathContext::VALUE);
                }
                this->visit_path_params(e.use);
                for (auto& trait : e.traits) this->visit_trait_path(trait);
            }
            TU_ARMA(Array, e) { this->visit_type(e.inner); if (auto* size = e.size.opt_Unevaluated()) this->visit_constgeneric(*size); }
            TU_ARMA(Slice, e) this->visit_type(e.inner);
            TU_ARMA(Tuple, e) for (auto& inner : e) this->visit_type(inner);
            TU_ARMA(Borrow, e) this->visit_type(e.inner);
            TU_ARMA(Pointer, e) this->visit_type(e.inner);
            TU_ARMA(NamedFunction, e) this->visit_path(e.path, ::HIR::Visitor::PathContext::VALUE);
            TU_ARMA(Function, e) { for (auto& arg : e.argTypes) this->visit_type(arg); this->visit_type(e.mRettype); }
            TU_ARMA(NodeType, e) {}
            }

            curParams = saved_params.first;
            curParamsLevel = saved_params.second;

            if (data.is_ErasedType()) {
                selfTypes.pop_back();
            }

            if (data.is_Borrow()) {
                currentLifetime.pop_back();
            }


            if (auto* e = data.opt_TraitObject()) {
                visit_lifetime(sp, e->lifetime);
            }

            if (auto* e = data.opt_ErasedType()) {
                for (auto& lft : e->lifetimeBounds) visit_lifetime(sp, lft);
            }

            ty = crate.types.intern(mv$(data));

            if (const auto* e = ty->opt_Path()) {
                TU_MATCH(::HIR::Path::Data, (e->path.mData), (pe), (Generic, ), (UfcsUnknown, TODO(sp, "Should UfcsKnown be encountered here?");), (UfcsInherent, TRACE_FUNCTION_FR("UfcsInherent - " << ty, ty); mResolve.expand_associated_types(sp, ty);), (UfcsKnown, TRACE_FUNCTION_FR("UfcsKnown - " << ty, ty); mResolve.expand_associated_types(sp, ty);))
            }
        }

        void visit_generic_path(::HIR::GenericPath& p, PathContext pc) override {
            static Span sp;
            TRACE_FUNCTION_F("p = " << p);
            const auto& params = get_params_for_item(sp, crate, p.mPath, pc);
            auto& args = p.mParams;

            check_parameters(sp, params, args);
            DEBUG("p = " << p);

            ::HIR::Visitor::visit_generic_path(p, pc);
        }

    private:
        bool locate_trait_item_in_bounds(const Span& sp, ::HIR::Visitor::PathContext pc, const ::HIR::TypeData* tr, const ::HIR::GenericParams& params, ::HIR::Path::Data& pd) {
            //const auto& name = pd.as_UfcsUnknown().item;
            for (const auto& b : params.bounds) {
                TU_IFLET(::HIR::GenericBound, b, TraitBound, e, DEBUG("- " << e.type << " : " << e.trait.mPath); if (e.type == tr) {
                    DEBUG(" - Match");
                    if (locate_in_trait_and_set(sp, pc, e.trait.mPath, this->crate.get_trait_by_path(sp, e.trait.mPath.mPath), pd)) {
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
                    if (trait.values.find(e.item) != trait.values.end()) {
                        return true;
                    }
                    break;
                case ::HIR::Visitor::PathContext::TRAIT:
                    break;
                case ::HIR::Visitor::PathContext::TYPE:
                    if (trait.types.find(e.item) != trait.types.end()) {
                        return true;
                    }
                    break;
            }
            return false;
        }

        bool locate_in_trait_and_set(const Span& sp, ::HIR::Visitor::PathContext pc, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            if (locate_item_in_trait(pc, trait, pd)) {
                pd = get_ufcs_known(mv$(pd.as_UfcsUnknown()), make_generic_path(trait_path.mPath, trait), trait);
                return true;
            }
            // Search all supertraits
            for (const auto& pt : trait.allParentTraits) {
                if (locate_item_in_trait(pc, *pt.traitPtr, pd)) {
                    pd = get_ufcs_known(mv$(pd.as_UfcsUnknown()), make_generic_path(trait_path.mPath, trait), trait);
                    return true;
                }
            }
            return false;
        }

        bool set_from_impl(const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            const auto& type = e.type;
            return this->crate.find_trait_impls(trait_path.mPath, type, HIR::ResolvePlaceholdersNop(), [&](const auto& impl) {
                DEBUG("FOUND impl" << impl.mParams.fmt_args() << " " << trait_path.mPath << impl.traitArgs << " for " << impl.mType);
                // TODO: Check bounds
                for (const auto& bound : impl.mParams.bounds) {
                    DEBUG("- TODO: Bound " << bound);
                    return false;
                }
                pd = get_ufcs_known(mv$(e), make_generic_path(trait_path.mPath, trait), trait);
                return true;
            });
        }

        bool locate_in_trait_impl_and_set(::HIR::Visitor::PathContext pc, const ::HIR::GenericPath& trait_path, const ::HIR::Trait& trait, ::HIR::Path::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            if (this->locate_item_in_trait(pc, trait, pd)) {
                return this->set_from_impl(trait_path, trait, pd);
            } else {
                DEBUG("- Item " << e.item << " not in trait " << trait_path.mPath);
            }

            // Search supertraits (recursively)
            for (const auto& pt : trait.allParentTraits) {
                if (this->locate_item_in_trait(pc, *pt.traitPtr, pd)) {
                    // TODO: Monomorphise params?
                    return set_from_impl(pt.mPath, *pt.traitPtr, pd);
                } else {
                }
            }
            return false;
        }

        ::HIR::GenericPath make_generic_path(::HIR::SimplePath sp, const ::HIR::Trait& trait) {
            auto trait_path_g = ::HIR::GenericPath(mv$(sp));
            for (unsigned int i = 0; i < trait.mParams.types.size(); i++) {
                trait_path_g.mParams.types.push_back(crate.types.generic(trait.mParams.types[i].mName, i));
            }
            return trait_path_g;
        }

        ::HIR::GenericPath get_current_trait_gp() const {
            assert(currentTraitPath);
            assert(currentTrait);
            auto trait_path = ::HIR::GenericPath(currentTraitPath->get_simple_path());
            for (unsigned int i = 0; i < currentTrait->mParams.types.size(); i++) {
                trait_path.mParams.types.push_back(crate.types.generic(currentTrait->mParams.types[i].mName, i));
            }
            return trait_path;
        }

        void visitPathUfcsUnknown(const Span& sp, ::HIR::Path& p, ::HIR::Visitor::PathContext pc) {
            TRACE_FUNCTION_FR("UfcsUnknown - p=" << p, p);
            auto& e = p.mData.as_UfcsUnknown();

            this->visit_type(e.type);
            this->visit_path_params(e.params);

            // Search for matching impls in current generic blocks
            if (mResolve.itemGenerics != nullptr && locate_trait_item_in_bounds(sp, pc, e.type, *mResolve.itemGenerics, p.mData)) {
                return;
            }
            if (mResolve.implGenerics != nullptr && locate_trait_item_in_bounds(sp, pc, e.type, *mResolve.implGenerics, p.mData)) {
                return;
            }

            if (const auto* te = e.type->opt_Generic()) {
                // If processing a trait, and the type is 'Self', search for the type/method on the trait
                // - TODO: This could be encoded by a `Self: Trait` bound in the generics, but that may have knock-on issues?
                if (te->name == "Self" && currentTrait) {
                    auto trait_path = this->get_current_trait_gp();
                    if (this->locate_in_trait_and_set(sp, pc, trait_path, *currentTrait, p.mData)) {
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
                            if (impl.methods.find(e.item) == impl.methods.end()) {
                                return false;
                            }
                            // Found it, just keep going (don't care about details here)
                            break;
                        case ::HIR::Visitor::PathContext::TRAIT:
                            return false;
                        case ::HIR::Visitor::PathContext::TYPE:
                            if (impl.types.find(e.item) == impl.types.end()) {
                                return false;
                            }
                            break;
                    }

                    return true;
                })) {
                    auto new_data = ::HIR::Path::Data::make_UfcsInherent({mv$(e.type), mv$(e.item), mv$(e.params)});
                    p.mData = mv$(new_data);
                    DEBUG("- Resolved, replace with " << p);
                    return;
                }
                // 2. Search all impls of in-scope traits for this method on this type
                for (const auto& trait_info : traits) {
                    const auto& trait = *trait_info.second;

                    switch (pc) {
                        case ::HIR::Visitor::PathContext::VALUE:
                            if (trait.values.find(e.item) == trait.values.end()) {
                                continue;
                            }
                            break;
                        case ::HIR::Visitor::PathContext::TRAIT:
                        case ::HIR::Visitor::PathContext::TYPE:
                            if (trait.types.find(e.item) == trait.types.end()) {
                                continue;
                            }
                            break;
                    }
                    DEBUG("- Trying trait " << *trait_info.first);

                    auto trait_path = ::HIR::GenericPath(*trait_info.first);
                    for (unsigned int i = 0; i < trait.mParams.types.size(); i++) {
                        trait_path.mParams.types.push_back(crate.types.infer());
                    }

                    // TODO: Search supertraits
                    // TODO: Should impls be searched first, or item names?
                    // - Item names add complexity, but impls are slower
                    if (this->locate_in_trait_impl_and_set(pc, mv$(trait_path), trait, p.mData)) {
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
                (p.mData),
                (e),
                (Generic, this->visit_generic_path(e, pc);),
                (
                    UfcsKnown, this->visit_type(e.type); selfTypes.push_back(e.type); this->visit_generic_path(e.trait, ::HIR::Visitor::PathContext::TRAIT); selfTypes.pop_back();
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
            for (auto& tps : params.types) {
                this->visit_type(tps.defaultValue);
            }

            for (auto& bound : params.bounds) {
                TU_MATCH_HDRA( (bound), {)
                TU_ARMA(Lifetime, e) {
                    }
                    TU_ARMA(TypeLifetime, e) {
                        this->visit_type(e.type);
                    }
                    TU_ARMA(TraitBound, e) {
                        this->visit_type(e.type);
                        selfTypes.push_back(e.type);
                        this->visit_trait_path(e.trait);
                        selfTypes.pop_back();
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
            currentTrait = &item;
            currentTraitPath = &p;

            auto _ = mResolve.set_impl_generics(MetadataType::TraitObject, item.mParams);
            auto self = crate.types.self();
            selfTypes.push_back(self);
            ::HIR::Visitor::visit_trait(p, item);
            selfTypes.pop_back();

            currentTrait = nullptr;
        }

        void visit_struct(::HIR::ItemPath p, ::HIR::Struct& item) override {
            auto _ = mResolve.set_impl_generics(item.structMarkings.dst_type, item.mParams);
            ::HIR::Visitor::visit_struct(p, item);
        }

        void visit_union(::HIR::ItemPath p, ::HIR::Union& item) override {
            auto _ = mResolve.set_impl_generics(MetadataType::None, item.mParams);
            ::HIR::Visitor::visit_union(p, item);
        }

        void visit_enum(::HIR::ItemPath p, ::HIR::Enum& item) override {
            auto _ = mResolve.set_impl_generics(MetadataType::None, item.mParams);
            ::HIR::Visitor::visit_enum(p, item);
        }

        void visit_associatedtype(::HIR::ItemPath p, ::HIR::AssociatedType& item) override {
            // Push `Self = <Self as CurTrait>::Type` for processing defaults in the bounds.
            auto path_aty = ::HIR::Path(crate.types.self(), this->get_current_trait_gp(), p.get_name());
            auto ty_aty = crate.types.path(mv$(path_aty), ::HIR::TypePathBinding::make_Opaque({}));
            selfTypes.push_back(ty_aty);

            ::HIR::Visitor::visit_associatedtype(p, item);

            selfTypes.pop_back();
        }

        void visit_type_alias(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            // Ignore type aliases, they don't have to typecheck.
        }

        void visit_inherent_type(::HIR::ItemPath p, ::HIR::TypeAlias& item) override {
            auto _ = mResolve.set_item_generics(item.mParams);
            auto saved_params = std::make_pair(curParams, curParamsLevel);
            curParams = &item.mParams;
            curParamsLevel = 1;
            ::HIR::Visitor::visit_inherent_type(p, item);
            curParams = saved_params.first;
            curParamsLevel = saved_params.second;
        }

        void add_lifetime_bounds_for_impl_type(const Span& sp, HIR::GenericParams& dst, const ::HIR::TypeData* ty) {
            // REF: rustc-1.29.0-src/src/vendor/clap/src/args/arg.rs:54 - Omitted lifetime bounds

            // https://rust-lang.github.io/rfcs/2089-implied-bounds.html ?
            // HACK: Just grab the lifetime bounds from a path type
            if (ty->is_Path() && ty->as_Path().path.mData.is_Generic()) {
                const auto& gp = ty->as_Path().path.mData.as_Generic();
                const auto& ti = mResolve.crate.get_typeitem_by_path(sp, gp.mPath);

                const HIR::GenericParams* params = nullptr;
                if (const auto* e = ti.opt_Struct()) {
                    params = &e->mParams;
                } else if (const auto* e = ti.opt_Enum()) {
                    params = &e->mParams;
                } else if (const auto* e = ti.opt_Union()) {
                    params = &e->mParams;
                } else {
                    DEBUG("TODO: Obtain bounds from " << ti.tag_str());
                }

                if (params) {
                    MonomorphStatePtr ms(crate.types, nullptr, &gp.mParams, nullptr);
                    for (const auto& b : params->bounds) {
                        if (const auto* be = b.opt_Lifetime()) {
                            dst.bounds.push_back(HIR::GenericBound::make_Lifetime({ms.monomorph_lifetime(sp, be->test), ms.monomorph_lifetime(sp, be->valid_for)}));
                        }
                    }
                }
            }
        }

        void visit_type_impl(::HIR::TypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType);
            auto _ = mResolve.set_impl_generics(impl.mType, impl.mParams);
            selfTypes.push_back(impl.mType);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.mParams;
                curParamsLevel = 0;
                this->visit_type(impl.mType);
                curParams = nullptr;
            }

            // Propagate bounds from the type
            add_lifetime_bounds_for_impl_type(Span(), impl.mParams, impl.mType);

            ::HIR::Visitor::visit_type_impl(impl);
            // TODO: Check that the type is valid

            selfTypes.pop_back();
        }

        void visit_trait_impl(const ::HIR::SimplePath& trait_path, ::HIR::TraitImpl& impl) override {
            static Span sp;
            TRACE_FUNCTION_F("impl" << impl.mParams.fmt_args() << " " << trait_path << impl.traitArgs << " for " << impl.mType);
            auto _ = mResolve.set_impl_generics(impl.mType, impl.mParams);
            selfTypes.push_back(impl.mType);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.mParams;
                curParamsLevel = 0;
                this->visit_type(impl.mType);
                this->visit_path_params(impl.traitArgs);
                curParams = nullptr;
            }

            // Propagate bounds from the type
            add_lifetime_bounds_for_impl_type(Span(), impl.mParams, impl.mType);

            ::HIR::Visitor::visit_trait_impl(trait_path, impl);
            selfTypes.pop_back();

            // TODO: Check that the type+trait is valid
            // - And fix bad elided liftimes (match annotations if they were elided)
            {
                const auto& trait = mResolve.crate.get_trait_by_path(sp, trait_path);
                for (auto& e : impl.methods) {
                    auto _ = mResolve.set_item_generics(e.second.data.mParams);

                    const auto v_it = trait.values.find(e.first);
                    if (v_it == trait.values.end() || !v_it->second.is_Function()) {
                        ERROR(sp, E0000, "Trait " << trait_path << " doesn't have a method named " << e.first);
                    }
                    auto& impl_fcn = e.second.data;
                    const auto& trait_fcn = v_it->second.as_Function();

                    auto fcn_params = trait_fcn.mParams.make_nop_params(crate.types, 1);
                    MonomorphStatePtr ms{crate.types, impl.mType, &impl.traitArgs, &fcn_params};
                    HIR::TypeRef tmp;
                    auto maybe_monomorph = [&](const HIR::TypeData* ty) -> const HIR::TypeData* {
                        if (monomorphise_type_needed(ty)) {
                            tmp = ms.monomorph_type(sp, ty);
                            mResolve.expand_associated_types(sp, tmp);
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
                    if (impl_fcn.mParams.types.size() != trait_fcn.mParams.types.size()) {
                        failures.push_back(FMT("Mismatched type param count (expected " << trait_fcn.mParams.types.size() << ", got " << impl_fcn.mParams.types.size() << ")"));
                    }
                    // Different logic for lifetimes, only want to check un-elided lifetimes
                    // - Well, elided lifetimes can overlap non-elided ones (as long as they're identical)
                    if (impl_fcn.mParams.values.size() != trait_fcn.mParams.values.size()) {
                        failures.push_back(FMT("Mismatched const param count (expected " << trait_fcn.mParams.values.size() << ", got " << impl_fcn.mParams.values.size() << ")"));
                    }
                    // -- Arguments
                    if (impl_fcn.mArgs.size() != trait_fcn.mArgs.size()) {
                        failures.push_back(FMT("Mismatched argument count (expected " << trait_fcn.mArgs.size() << ", got " << impl_fcn.mArgs.size() << ")"));
                    }
                    if (impl_fcn.receiver != trait_fcn.receiver) {
                        failures.push_back(FMT("Receiver type")); //"(expected " << trait_fcn.m_receiver << ", got " << impl_fcn.m_receiver));
                    }
                    for (size_t i = 0; i < std::min(impl_fcn.mArgs.size(), trait_fcn.mArgs.size()); i++) {
                        if (!(i == 0 && (trait_fcn.receiver == HIR::Function::Receiver::Free || impl_fcn.receiver == HIR::Function::Receiver::Free))) {
                            // Check the type.
                            // - Also, fix lifetime elision?
                            const auto& exp_ty = maybe_monomorph(trait_fcn.mArgs[i].second);
                            /*const*/ auto& has_ty = impl_fcn.mArgs[i].second;

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
                                if (const auto* path_p = ty_p->path.mData.opt_UfcsKnown()) {
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

                    const auto& exp_ret_ty1 = maybe_monomorph(trait_fcn.returnType);
                    if (!exp_ret_ty1->match_test_generics(sp, impl_fcn.returnType, HIR::ResolvePlaceholdersNop(), match_cb)) {
                        failures.push_back(
                            FMT("Mismatched return type:\n"
                                << "  Expected " << exp_ret_ty1 << "\n"
                                << "  Found    " << impl_fcn.returnType)
                        );
                    }
                    HIR::TypeRef exp_ret_ty_real;
                    const auto& exp_ret_ty = match_cb.mapping.empty() ? exp_ret_ty1 : (exp_ret_ty_real = clone_ty_with(crate.types, sp, exp_ret_ty1, [&](const ::HIR::TypeData* ref, ::HIR::TypeRef& out) -> bool {
                        if (const auto* ty_p = ref->opt_Path()) {
                            if (const auto* path_p = ty_p->path.mData.opt_UfcsKnown()) {
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
                                                 os << "    fn " << e.first << trait_fcn.mParams.fmt_args() << "(";
                                                 for (const auto& a : trait_fcn.mArgs) {
                                                     os << a.first << ": " << maybe_monomorph(a.second) << ", ";
                                                 }
                                                 os << ")\n";
                                                 os << "    -> " << maybe_monomorph(trait_fcn.returnType) << "\n";
                                                 os << "    " << trait_fcn.mParams.fmt_bounds();
                                             }
                                         )
                                      << "\n"
                                      << "Impl :\n"
                                      << FMT_CB(
                                             os,
                                             {
                                                 os << "    fn " << e.first << impl_fcn.mParams.fmt_args() << "(";
                                                 for (const auto& a : impl_fcn.mArgs) {
                                                     os << a.first << ": " << a.second << ", ";
                                                 }
                                                 os << ")\n";
                                                 os << "    -> " << impl_fcn.returnType << "\n";
                                                 os << "    " << impl_fcn.mParams.fmt_bounds();
                                             }
                                         )
                                      << "\n"
                                      << "in impl" << impl.mParams.fmt_args() << " " << trait_path << impl.traitArgs << " for " << impl.mType
                        );
                    }
                    // HACK: Replace all types (which should be functionally identical) so lifetimes match
                    // - This is needed for monomorphisation to work properly?
                    // REF: rustc-1.29.0/src/vendor/serde/src/private/de.rs:1379
                    // Counter-ref: rustc-1.54.0
                    // Update AFTER the checks
                    DEBUG("Replace generic block's lifetimes with " << trait_fcn.mParams.fmt_args());
                    impl_fcn.mParams.mLifetimes = trait_fcn.mParams.mLifetimes;
                    // Replace the lifetime bounds too (undoes some potential confusion from elision)
                    {
                        auto& bl = impl_fcn.mParams.bounds;
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
                    for (const auto& b : trait_fcn.mParams.bounds) {
                        TU_MATCH_HDRA( (b), { )
                        default:
                            break;
                            TU_ARMA(TypeLifetime, be) {
                                impl_fcn.mParams.bounds.push_back(::HIR::GenericBound::make_TypeLifetime({ms.monomorph_type(sp, be.type), ms.monomorph_lifetime(sp, be.valid_for)}));
                            }
                            TU_ARMA(Lifetime, be) {
                                impl_fcn.mParams.bounds.push_back(::HIR::GenericBound::make_Lifetime({ms.monomorph_lifetime(sp, be.test), ms.monomorph_lifetime(sp, be.valid_for)}));
                            }
                        }
                    }

                    // HACK: Clone the expected type, so the lifetimes match.
                    DEBUG("Updating < " << impl.mType << " as " << trait_path << impl.traitArgs << " >::" << e.first);
                    impl_fcn.returnType = exp_ret_ty;
                    for (size_t i = 0; i < std::min(impl_fcn.mArgs.size(), trait_fcn.mArgs.size()); i++) {
                        DEBUG("ARG" << i << "> " << trait_fcn.mArgs[i].second);
                        impl_fcn.mArgs[i].second = mResolve.monomorph_expand(sp, trait_fcn.mArgs[i].second, ms);
                    }
                    DEBUG("Updated < " << impl.mType << " as " << trait_path << impl.traitArgs << " >::" << e.first);

                    DEBUG(FMT_CB(os, {
                        os << "fn " << e.first << impl_fcn.mParams.fmt_args() << "(";
                        for (const auto& a : impl_fcn.mArgs) {
                            os << a.first << ": " << a.second << ", ";
                        }
                        os << ")";
                        os << impl_fcn.mParams.fmt_bounds();
                    }));
                }
                for (const auto& e : impl.constants) {
                    const auto& vi = trait.values.at(e.first);
                    if (!vi.is_Constant()) {
                        ERROR(sp, E0000, "Trait " << trait_path << " doesn't have a constant named " << e.first);
                    }
                    const auto& impl_const = e.second.data;
                    const auto& trait_const = vi.as_Constant();

                    // Check type
                }
                for (const auto& e : impl.statics) {
                    const auto& vi = trait.values.at(e.first);
                    if (!vi.is_Static()) {
                        ERROR(sp, E0000, "Trait " << trait_path << " doesn't have a static named " << e.first);
                    }
                    const auto& impl_static = e.second.data;
                    const auto& trait_static = vi.as_Static();

                    // Check type
                }
                for (const auto& e : trait.types) {
                    const auto& trait_type = trait.types.at(e.first);
                    const auto& impl_type = e.second;

                    // Check that the bounds fit
                }
            }
        }

        void visit_marker_impl(const ::HIR::SimplePath& trait_path, ::HIR::MarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << trait_path << " for " << impl.mType << " { }");
            auto _ = mResolve.set_impl_generics(impl.mType, impl.mParams);
            selfTypes.push_back(impl.mType);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.mParams;
                curParamsLevel = 0;
                this->visit_type(impl.mType);
                this->visit_path_params(impl.traitArgs);
                curParams = nullptr;
            }

            // Propagate bounds from the type/trait
            add_lifetime_bounds_for_impl_type(Span(), impl.mParams, impl.mType);

            ::HIR::Visitor::visit_marker_impl(trait_path, impl);
            // TODO: Check that the type+trait is valid

            selfTypes.pop_back();
        }

        void visit_function(::HIR::ItemPath p, ::HIR::Function& item) override {
            TRACE_FUNCTION_F(p);

            if (mResolve.crate.get_lang_item_path_opt("sized").components().empty()) {
                ERROR(Span(), E0000, "requires `sized` lang_item");
            }

            auto _ = mResolve.set_item_generics(item.mParams);
            // NOTE: Superfluous... except that it makes the params valid for the return type.
            visit_params(item.mParams);

            fcnPtr = &item;
            auto first_elided_lifetime_idx = item.mParams.mLifetimes.size();

            // Visit arguments
            // - Used to convert `impl Trait` in argument position into generics
            // - Done first so the path in return-position `impl Trait` is valid
            curParams = &item.mParams;
            curParamsLevel = 1;
            for (auto& arg : item.mArgs) {
                TRACE_FUNCTION_F("ARG " << arg);
                visit_type(arg.second);
            }
            curParams = nullptr;

            // Get output lifetime
            // - Try `&self`'s lifetime (if it was an elided lifetime)
            HIR::LifetimeRef elided_output_lifetime;
            if (item.receiver != HIR::Function::Receiver::Free) {
                if (const auto* b = item.mArgs[0].second->opt_Borrow()) {
                    // If this was an elided lifetime.
                    if (b->lifetime.is_param() && (b->lifetime.binding >> 8) == 1 && (b->lifetime.binding & 0xFF) > first_elided_lifetime_idx) {
                        elided_output_lifetime = b->lifetime;
                    }
                }
            }
            // - OR, look for only one elided lifetime
            if (elided_output_lifetime == HIR::LifetimeRef()) {
                if (item.mParams.mLifetimes.size() == first_elided_lifetime_idx + 1) {
                    elided_output_lifetime = HIR::LifetimeRef(256 + first_elided_lifetime_idx);
                }
            }
            // If present, set it (push to the stack)
            assert(currentLifetime.empty());
            if (elided_output_lifetime != HIR::LifetimeRef()) {
                currentLifetime.push_back(&elided_output_lifetime);
            }

            // Visit return type (populates path for `impl Trait` in return position
            fcnPath = &p;
            fcnErasedCount = 0;
            {
                TRACE_FUNCTION_F("RET " << item.returnType);
                visit_type(item.returnType);
            }
            fcnPath = nullptr;
            fcnPtr = nullptr;

            if (elided_output_lifetime != HIR::LifetimeRef()) {
                currentLifetime.pop_back();
            }
            assert(currentLifetime.empty());

            if (item.receiver == HIR::Function::Receiver::Custom) {
                ASSERT_BUG(Span(), item.receiverType, "Custom receiver without a receiver type");
                this->visit_type(*item.receiverType);
            }
            ::HIR::Visitor::visit_function(p, item);
        }
    };
}

void TypecheckModuleLevel(::HIR::Crate& crate) {
    Visitor v{crate};
    v.visit_crate(crate);
}
