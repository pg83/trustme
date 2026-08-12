#include "hir_typeck_main_bindings.h"
#include "hir_typeck_main_bindings.h"

#include "hir_hir.h"
#include "hir_expr.h"
#include "hir_visitor.h"
#include "hir_typeck_static.h"

#include <algorithm>

namespace {
    typedef ::std::vector<::std::pair<HIRPattern, HIRTypeRef>> tArgs;

    class ExprVisitorValidate: public HIRExprVisitor {
        const StaticTraitResolve& mResolve;
        //const t_args&   m_args;
        const HIRTypeData* realRetType;
        HIRTypeRef retType;

        struct RetTarget {
            const HIRTypeData* retType;
            const HIRTypeData* yieldType;

            RetTarget(const HIRTypeData* retType)
                : retType(retType)
                , yieldType(nullptr)
            {
            }

            RetTarget(const HIRTypeData* retType, const HIRTypeData* yieldType)
                : retType(retType)
                , yieldType(yieldType)
            {
            }
        };

        ::std::vector<RetTarget> closureRetTypes;
        ::std::vector<const HIRExprNodeLoop*> loops;
        //const ::HIR::ExprPtr* m_cur_expr;

        HIRSimplePath mLangIndex;

    public:
        bool expandErasedTypes;

        ExprVisitorValidate(const StaticTraitResolve& res, const tArgs& args, const HIRTypeData* retType)
            : mResolve(res)
            ,
            //m_args(args),
            realRetType(retType)
            , expandErasedTypes(true)
        {
            mLangIndex = mResolve.crate.getLangItemPathOpt("index");
        }

        void visitRoot(HIRExprPtr& nodePtr) {
            const auto& sp = nodePtr->span();

            // Monomorphise erased type
            retType = cloneTyWith(mResolve.crate.types, sp, realRetType, [&](const auto& tpl, auto& rv) -> bool {
                if (const auto* e = tpl->opt_ErasedType()) {
                    if (const auto* ee = e->inner.opt_Fcn()) {
                        ASSERT_BUG(sp, ee->index < nodePtr.erasedTypes.size(), "Erased type index OOB - " << ee->origin << " " << ee->index << " >= " << nodePtr.erasedTypes.size());
                        // TODO: Check that erased type bounds are still met
                        rv = nodePtr.erasedTypes[ee->index];
                        return true;
                    }
                }
                return false;
            });
            mResolve.expandAssociatedTypes(sp, retType);

            nodePtr->visit(*this);

            checkTypesEqual(sp, retType, nodePtr->resType);
        }

        void visit(HIRExprNodeBlock& node) override {
            TRACE_FUNCTION_F(&node << " { ... }");
            for (auto& n : node.nodes) {
                n->visit(*this);
            }
            if (node.valueNode) {
                node.valueNode->visit(*this);
                checkTypesEqual(node.span(), node.resType, node.valueNode->resType);
            }
        }

        void visit(HIRExprNodeConstBlock& node) override {
            TRACE_FUNCTION_F(&node << " const { ... }");
            node.inner->visit(*this);
            checkTypesEqual(node.span(), node.resType, node.inner->resType);
        }

        void visit(HIRExprNodeAsm& node) override {
            TRACE_FUNCTION_F(&node << " llvm_asm! ...");

            // TODO: Check result types
            for (auto& v : node.outputs) {
                v.value->visit(*this);
            }
            for (auto& v : node.inputs) {
                v.value->visit(*this);
            }
        }

        void visit(HIRExprNodeAsm2& node) override {
            TRACE_FUNCTION_F(&node << " asm! ...");

            // TODO: Check result types
            for (auto& v : node.mParams) {
                TU_MATCH_HDRA( (v), { )
                TU_ARMA(Const, e) {
                        visitNodePtr(e);
                    }
                    TU_ARMA(Sym, e) {
                    }
                    TU_ARMA(RegSingle, e) {
                        visitNodePtr(e.val);
                    }
                    TU_ARMA(Reg, e) {
                        if (e.valIn) {
                            visitNodePtr(e.valIn);
                        }
                        if (e.valOut) {
                            visitNodePtr(e.valOut);
                        }
                    }
                }
            }
        }

        void visit(HIRExprNodeReturn& node) override {
            TRACE_FUNCTION_F(&node << " return ...");
            // Check against return type
            const auto* retTy = (this->closureRetTypes.size() > 0 ? this->closureRetTypes.back().retType : this->retType);
            checkTypesEqual(retTy, node.mValue);
            node.mValue->visit(*this);
        }

        void visit(HIRExprNodeYield& node) override {
            TRACE_FUNCTION_F(&node << " yield ...");
            ASSERT_BUG(node.span(), !this->closureRetTypes.empty(), "Yield outside a generator closure");
            ASSERT_BUG(node.span(), this->closureRetTypes.back().yieldType, "Yield outside a generator closure");
            checkTypesEqual(this->closureRetTypes.back().yieldType, node.mValue);
            node.mValue->visit(*this);
        }

        void visit(HIRExprNodeAWait& node) override {
            node.mValue->visit(*this);
            auto t = mResolve.crate.types.path(HIRPath(node.mValue->resType, mResolve.mLangFuture, "Output"), {});
            mResolve.expandAssociatedTypes(node.span(), t);
            checkTypesEqual(node.span(), node.resType, t);
        }

        void visit(HIRExprNodeLoop& node) override {
            TRACE_FUNCTION_F(&node << " loop { ... }");
            loops.push_back(&node);
            node.mCode->visit(*this);
            loops.pop_back();
        }

        void visit(HIRExprNodeLoopControl& node) override {
            TRACE_FUNCTION_F(&node << " " << (node.isContinue ? "continue" : "break") << " '" << node.label);

            if (node.mValue) {
                node.mValue->visit(*this);
            }

            if (!node.isContinue) {
                HIRTypeRef unit = mResolve.crate.types.unit();
                const auto& ty = (node.mValue ? node.mValue->resType : unit);

                auto it = ::std::find(this->loops.rbegin(), this->loops.rend(), node.targetNode);
                ASSERT_BUG(node.span(), it != this->loops.rend(), "Loop target node not found in the loop stack");

                DEBUG("Breaking to " << node.targetNode << ", type " << node.targetNode->resType);
                checkTypesEqual(node.span(), node.targetNode->resType, ty);
            }
        }

        void visit(HIRExprNodeLet& node) override {
            TRACE_FUNCTION_F(&node << " let " << node.pattern << ": " << node.mType);
            if (node.mValue) {
                checkPattern(node.pattern, node.mValue->resType);
                checkTypesEqual(node.span(), node.mType, node.mValue->resType);
                node.mValue->visit(*this);
            }
        }

        void visit(HIRExprNodeMatch& node) override {
            TRACE_FUNCTION_F(&node << " match ...");
            node.mValue->visit(*this);
            for (auto& arm : node.arms) {
                for (const auto& pat : arm.patterns) {
                    checkPattern(pat, node.mValue->resType);
                }
                checkTypesEqual(node.span(), node.resType, arm.mCode->resType);
                arm.mCode->visit(*this);
            }
        }

        void visit(HIRExprNodeAssign& node) override {
            TRACE_FUNCTION_F(&node << "... ?= ...");

            if (node.op == HIRExprNodeAssign::Op::None) {
                checkTypesEqual(node.span(), node.slot->resType, node.mValue->resType);
            } else {
                // Type inferrence using the +=
                // - "" as type name to indicate that it's just using the trait magic?
                const char* langItem = nullptr;
                auto operatorKind = TypeckPrimitiveOperator::None;
                switch (node.op) {
                    case HIRExprNodeAssign::Op::None:
                        throw "";
                    case HIRExprNodeAssign::Op::Add:
                        langItem = "add_assign";
                        operatorKind = TypeckPrimitiveOperator::AddAssign;
                        break;
                    case HIRExprNodeAssign::Op::Sub:
                        langItem = "sub_assign";
                        operatorKind = TypeckPrimitiveOperator::SubAssign;
                        break;
                    case HIRExprNodeAssign::Op::Mul:
                        langItem = "mul_assign";
                        operatorKind = TypeckPrimitiveOperator::MulAssign;
                        break;
                    case HIRExprNodeAssign::Op::Div:
                        langItem = "div_assign";
                        operatorKind = TypeckPrimitiveOperator::DivAssign;
                        break;
                    case HIRExprNodeAssign::Op::Mod:
                        langItem = "rem_assign";
                        operatorKind = TypeckPrimitiveOperator::RemAssign;
                        break;
                    case HIRExprNodeAssign::Op::And:
                        langItem = "bitand_assign";
                        operatorKind = TypeckPrimitiveOperator::BitAndAssign;
                        break;
                    case HIRExprNodeAssign::Op::Or:
                        langItem = "bitor_assign";
                        operatorKind = TypeckPrimitiveOperator::BitOrAssign;
                        break;
                    case HIRExprNodeAssign::Op::Xor:
                        langItem = "bitxor_assign";
                        operatorKind = TypeckPrimitiveOperator::BitXorAssign;
                        break;
                    case HIRExprNodeAssign::Op::Shr:
                        langItem = "shr_assign";
                        operatorKind = TypeckPrimitiveOperator::ShrAssign;
                        break;
                    case HIRExprNodeAssign::Op::Shl:
                        langItem = "shl_assign";
                        operatorKind = TypeckPrimitiveOperator::ShlAssign;
                        break;
                }
                assert(langItem);
                if (!primitiveOperatorHasBuiltin(operatorKind, node.slot->resType, node.mValue->resType)) {
                    const auto& traitPath = this->getLangItemPath(node.span(), langItem);
                    checkTraitBound(node.span(), traitPath, {node.mValue->resType}, node.slot->resType);
                }
            }

            node.slot->visit(*this);
            node.mValue->visit(*this);
        }

        void visit(HIRExprNodeBinOp& node) override {
            TRACE_FUNCTION_F(&node << "... " << HIRExprNodeBinOp::opname(node.op) << " ...");

            switch (node.op) {
                case HIRExprNodeBinOp::Op::CmpEqu:
                case HIRExprNodeBinOp::Op::CmpNEqu:
                case HIRExprNodeBinOp::Op::CmpLt:
                case HIRExprNodeBinOp::Op::CmpLtE:
                case HIRExprNodeBinOp::Op::CmpGt:
                case HIRExprNodeBinOp::Op::CmpGtE: {
                    checkTypesEqual(node.span(), mResolve.crate.types.primitive(HIRCoreType::Bool), node.resType);

                    const char* itemName = nullptr;
                    switch (node.op) {
                        case HIRExprNodeBinOp::Op::CmpEqu:
                            itemName = "eq";
                            break;
                        case HIRExprNodeBinOp::Op::CmpNEqu:
                            itemName = "eq";
                            break;
                        case HIRExprNodeBinOp::Op::CmpLt:
                            itemName = "partial_ord";
                            break;
                        case HIRExprNodeBinOp::Op::CmpLtE:
                            itemName = "partial_ord";
                            break;
                        case HIRExprNodeBinOp::Op::CmpGt:
                            itemName = "partial_ord";
                            break;
                        case HIRExprNodeBinOp::Op::CmpGtE:
                            itemName = "partial_ord";
                            break;
                        default:
                            break;
                    }
                    assert(itemName);
                    auto operatorKind = node.op == HIRExprNodeBinOp::Op::CmpEqu || node.op == HIRExprNodeBinOp::Op::CmpNEqu ? TypeckPrimitiveOperator::Equal : TypeckPrimitiveOperator::Order;
                    if (!primitiveOperatorHasBuiltin(operatorKind, node.left->resType, node.right->resType)) {
                        const auto& opTrait = this->getLangItemPath(node.span(), itemName);
                        checkTraitBound(node.span(), opTrait, {node.right->resType}, node.left->resType);
                    }
                    break;
                }

                case HIRExprNodeBinOp::Op::BoolAnd:
                case HIRExprNodeBinOp::Op::BoolOr:
                    // No validation needed, result forced in typeck
                    break;
                default: {
                    const char* itemName = nullptr;
                    auto operatorKind = TypeckPrimitiveOperator::None;
                    switch (node.op) {
                        case HIRExprNodeBinOp::Op::CmpEqu:
                            throw "";
                        case HIRExprNodeBinOp::Op::CmpNEqu:
                            throw "";
                        case HIRExprNodeBinOp::Op::CmpLt:
                            throw "";
                        case HIRExprNodeBinOp::Op::CmpLtE:
                            throw "";
                        case HIRExprNodeBinOp::Op::CmpGt:
                            throw "";
                        case HIRExprNodeBinOp::Op::CmpGtE:
                            throw "";
                        case HIRExprNodeBinOp::Op::BoolAnd:
                            throw "";
                        case HIRExprNodeBinOp::Op::BoolOr:
                            throw "";

                        case HIRExprNodeBinOp::Op::Add:
                            itemName = "add";
                            operatorKind = TypeckPrimitiveOperator::Add;
                            break;
                        case HIRExprNodeBinOp::Op::Sub:
                            itemName = "sub";
                            operatorKind = TypeckPrimitiveOperator::Sub;
                            break;
                        case HIRExprNodeBinOp::Op::Mul:
                            itemName = "mul";
                            operatorKind = TypeckPrimitiveOperator::Mul;
                            break;
                        case HIRExprNodeBinOp::Op::Div:
                            itemName = "div";
                            operatorKind = TypeckPrimitiveOperator::Div;
                            break;
                        case HIRExprNodeBinOp::Op::Mod:
                            itemName = "rem";
                            operatorKind = TypeckPrimitiveOperator::Rem;
                            break;

                        case HIRExprNodeBinOp::Op::And:
                            itemName = "bitand";
                            operatorKind = TypeckPrimitiveOperator::BitAnd;
                            break;
                        case HIRExprNodeBinOp::Op::Or:
                            itemName = "bitor";
                            operatorKind = TypeckPrimitiveOperator::BitOr;
                            break;
                        case HIRExprNodeBinOp::Op::Xor:
                            itemName = "bitxor";
                            operatorKind = TypeckPrimitiveOperator::BitXor;
                            break;

                        case HIRExprNodeBinOp::Op::Shr:
                            itemName = "shr";
                            operatorKind = TypeckPrimitiveOperator::Shr;
                            break;
                        case HIRExprNodeBinOp::Op::Shl:
                            itemName = "shl";
                            operatorKind = TypeckPrimitiveOperator::Shl;
                            break;
                    }
                    assert(itemName);
                    if (!primitiveOperatorHasBuiltin(operatorKind, node.left->resType, node.right->resType)) {
                        const auto& opTrait = this->getLangItemPath(node.span(), itemName);
                        checkAssociatedType(node.span(), node.resType, opTrait, {node.right->resType}, node.left->resType, "Output");
                    }
                    break;
                }
            }

            node.left->visit(*this);
            node.right->visit(*this);
        }

        void visit(HIRExprNodeUniOp& node) override {
            TRACE_FUNCTION_F(&node << " " << HIRExprNodeUniOp::opname(node.op) << "...");
            auto operatorKind = TypeckPrimitiveOperator::None;
            switch (node.op) {
                case HIRExprNodeUniOp::Op::Invert:
                    operatorKind = TypeckPrimitiveOperator::Not;
                    if (!primitiveOperatorHasBuiltin(operatorKind, node.mValue->resType)) {
                        checkAssociatedType(node.span(), node.resType, this->getLangItemPath(node.span(), "not"), {}, node.mValue->resType, "Output");
                    }
                    break;
                case HIRExprNodeUniOp::Op::Negate:
                    operatorKind = TypeckPrimitiveOperator::Neg;
                    if (!primitiveOperatorHasBuiltin(operatorKind, node.mValue->resType)) {
                        checkAssociatedType(node.span(), node.resType, this->getLangItemPath(node.span(), "neg"), {}, node.mValue->resType, "Output");
                    }
                    break;
            }
            node.mValue->visit(*this);
        }

        void visit(HIRExprNodeBorrow& node) override {
            TRACE_FUNCTION_F(&node << " &_ ...");
            checkTypesEqual(node.span(), node.resType, mResolve.crate.types.borrow(node.mType, node.mValue->resType));
            node.mValue->visit(*this);
        }

        void visit(HIRExprNodeRawBorrow& node) override {
            TRACE_FUNCTION_F(&node << " &raw _ ...");
            checkTypesEqual(node.span(), node.resType, mResolve.crate.types.pointer(node.mType, node.mValue->resType));
            node.mValue->visit(*this);
        }

        void visit(HIRExprNodeIndex& node) override {
            TRACE_FUNCTION_F(&node << " ... [ ... ]");
            checkAssociatedType(node.span(), node.resType, mLangIndex, {node.index->resType}, node.mValue->resType, "Output");

            node.mValue->visit(*this);
            node.index->visit(*this);
        }

        void visit(HIRExprNodeCast& node) override {
            TRACE_FUNCTION_F(&node << " " << node.mValue->resType << " as " << node.dstType);
            const Span& sp = node.span();
            DEBUG("Cast res type " << node.resType);

            const auto& srcTy = node.mValue->resType;
            const auto& dstTy = node.resType;

            if (dstTy == srcTy) {
                // Would be nice to delete it, but this is a readonly pass
                return;
            }

            // Check castability
            TU_MATCH_HDRA( ((*dstTy)), {)
            default:
                ERROR(sp, E0000, "Invalid cast to\n " << dstTy << "\n from\n " << srcTy);
                TU_ARMA(Pointer, de) {
                TU_MATCH_HDRA( ((*srcTy)), {)
                default:
                    ERROR(sp, E0000, "Invalid cast to " << dstTy << " from " << srcTy);
                        TU_ARMA(Pointer, se) {
                            // TODO: Sized check - can't cast to a fat pointer from a thin one
                            //}
                        }
                        TU_ARMA(Primitive, se) {
                            switch (se) {
                                case HIRCoreType::Bool:
                                case HIRCoreType::Char:
                                case HIRCoreType::Str:
                                case HIRCoreType::F32:
                                case HIRCoreType::F64:
                                    ERROR(sp, E0000, "Invalid cast to " << dstTy << " from " << srcTy);
                                default:
                                    break;
                            }
                            //}
                        }
                        break;
                        case HIRTypeData::TAG_Function:
                        case HIRTypeData::TAG_NamedFunction:
                            if (de.inner == mResolve.crate.types.unit() || de.inner == HIRCoreType::U8 || de.inner == HIRCoreType::I8) {
                            } else if (mResolve.typeIsSized(sp, de.inner)) {
                                // Allow it.
                            } else {
                                ERROR(sp, E0000, "Invalid cast to " << dstTy << " from " << srcTy);
                            }
                            TU_ARMA(Borrow, se) {
                                this->checkTypesEqual(sp, de.inner, se.inner);
                            }
                }
                }
                TU_ARMA(Function, de) {
                    // NOTE: cast fn() only valid from:
                    // - the same function pointer (already checked, but eventually could be a stripping of the path tag)
                    // - A capture-less closure
                TU_MATCH_HDRA( ((*srcTy)), {)
                default:
                    ERROR(sp, E0000, "Invalid cast to " << dstTy << " from " << srcTy);
                        break;
                        TU_ARMA(NamedFunction, se) {
                            // TODO: Check?
                        }
                        TU_ARMA(Function, se) {
                            if (se.isUnsafe != de.isUnsafe && se.isUnsafe) {
                                ERROR(sp, E0000, "Invalid cast to " << dstTy << " from " << srcTy << " - removing unsafe");
                            }
                            if (se.mAbi != de.mAbi) {
                                ERROR(sp, E0000, "Invalid cast to " << dstTy << " from " << srcTy << " - different ABI");
                            }
                            if (se.mRettype != de.mRettype) {
                                ERROR(sp, E0000, "Invalid cast to " << dstTy << " from " << srcTy << " - return type different");
                            }
                            if (se.argTypes.size() != de.argTypes.size()) {
                                ERROR(sp, E0000, "Invalid cast to " << dstTy << " from " << srcTy << " - argument count different");
                            }
                            for (size_t i = 0; i < se.argTypes.size(); i++) {
                                if (se.argTypes[i] != de.argTypes[i]) {
                                    ERROR(sp, E0000, "Invalid cast to " << dstTy << " from " << srcTy << " - argument " << i << " different");
                                }
                            }
                        }
                        TU_ARMA(NodeType, se) {
                            if (se.is_Closure()) {
                                // Allowed, but won't exist after expansion
                                // TODO: Check argument types
                            } else {
                                ERROR(sp, E0000, "Invalid cast to " << dstTy << " from " << srcTy << " - not a function");
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

        void visit(HIRExprNodeUnsize& node) override {
            TRACE_FUNCTION_F(&node << " ... : " << node.resType);
            const Span& sp = node.span();

            const auto& srcTy = node.mValue->resType;
            const auto& dstTy = node.resType;

            if (srcTy->is_Diverge()) {
                // Perfectly valid. (! can become anything)
            } else if (srcTy == dstTy) {
            } else if (srcTy->is_Borrow() && dstTy->is_Borrow()) {
                const auto& se = srcTy->as_Borrow();
                const auto& de = dstTy->as_Borrow();
                if (se.type != de.type) {
                    ERROR(sp, E0000, "Invalid unsizing operation to " << dstTy << " from " << srcTy << " - Borrow class mismatch");
                }
                const auto& srcTy = se.inner;
                const auto& dstTy = de.inner;

                const auto& langUnsize = mResolve.crate.getLangItemPathOpt("unsize");
                if (!langUnsize.components().empty()) {
                    // _ == < `src_ty` as Unsize< `dst_ty` >::""
                    checkTraitBound(sp, langUnsize, {dstTy}, srcTy);
                } else if (!mResolve.canUnsize(sp, dstTy, srcTy)) {
                    ERROR(sp, E0000, "Invalid unsizing operation to " << dstTy << " from " << srcTy);
                }
            } else if (srcTy->is_Borrow() || dstTy->is_Borrow()) {
                ERROR(sp, E0000, "Invalid unsizing operation to " << dstTy << " from " << srcTy);
            } else {
                const auto& langCoerceUnsized = this->getLangItemPath(node.span(), "coerce_unsized");
                // _ == < `src_ty` as CoerceUnsized< `dst_ty` >::""
                checkTraitBound(sp, langCoerceUnsized, {dstTy}, srcTy);
            }

            node.mValue->visit(*this);
        }

        void visit(HIRExprNodeDeref& node) override {
            TRACE_FUNCTION_F(&node << " *...");
            const auto& ty = node.mValue->resType;

            const bool builtin = node.traitUsed == HIRExprNodeDeref::TraitUsed::Builtin || (node.traitUsed == HIRExprNodeDeref::TraitUsed::Unknown && primitiveOperatorHasBuiltin(TypeckPrimitiveOperator::Deref, ty));
            if (builtin && ty->is_Pointer()) {
                checkTypesEqual(node.span(), node.resType, ty->as_Pointer().inner);
            } else if (builtin && ty->is_Borrow()) {
                checkTypesEqual(node.span(), node.resType, ty->as_Borrow().inner);
            } else {
                checkAssociatedType(node.span(), node.resType, this->getLangItemPath(node.span(), "deref"), {}, node.mValue->resType, "Target");
            }

            node.mValue->visit(*this);
        }

        void visit(HIRExprNodeEmplace& node) override {
            switch (node.mType) {
                case HIRExprNodeEmplace::Type::Noop:
                    assert(!node.place);

                    checkTypesEqual(node.span(), node.resType, node.mValue->resType);
                    break;
                case HIRExprNodeEmplace::Type::Boxer:
                    // TODO: Check trait and associated type
                    break;
                case HIRExprNodeEmplace::Type::Placer:
                    // TODO: Check trait
                    break;
            }

            if (node.place) {
                this->visitNodePtr(node.place);
            }
            this->visitNodePtr(node.mValue);
        }

        void visit(HIRExprNodeTupleVariant& node) override {
            TRACE_FUNCTION_F(&node << " " << node.mPath << "(...,) [" << (node.isStruct ? "struct" : "enum") << "]");
            const auto& sp = node.span();

            // - Create ivars in path, and set result type
            const auto& ty = node.resType;

            const tTupleFields* fieldsPtr = nullptr;
            ASSERT_BUG(sp, ty->is_Path(), "Result type of _TupleVariant isn't Path");
            TU_MATCH(HIRTypePathBinding, (ty->as_Path().binding), (e), (Unbound, BUG(sp, "Unbound type in _TupleVariant - " << ty);), (Opaque, BUG(sp, "Opaque type binding in _TupleVariant - " << ty);), (Enum, const auto& varName = node.mPath.mPath.components().back(); const auto& enm = *e; size_t idx = enm.findVariant(varName); const auto& varTy = enm.mData.as_Data()[idx].type; const auto& str = *varTy->as_Path().binding.as_Struct(); ASSERT_BUG(sp, str.mData.is_Tuple(), "Pointed variant of TupleVariant (" << node.mPath << ") isn't a Tuple"); fieldsPtr = &str.mData.as_Tuple();), (Union, BUG(sp, "Union in TupleVariant");), (ExternType, BUG(sp, "ExternType in TupleVariant");), (Struct, ASSERT_BUG(sp, e->mData.is_Tuple(), "Pointed struct in TupleVariant (" << node.mPath << ") isn't a Tuple"); fieldsPtr = &e->mData.as_Tuple();))
            assert(fieldsPtr);
            const tTupleFields& fields = *fieldsPtr;
            ASSERT_BUG(sp, fields.size() == node.mArgs.size(), "");

            // Bind fields with type params (coercable)
            // TODO: Remove use of m_arg_types (maybe assert that cache is correct?)
            for (unsigned int i = 0; i < node.mArgs.size(); i++) {
                const auto& desTyR = fields[i].ent;
                const auto* desTy = &desTyR;
                if (monomorphiseTypeNeeded(desTyR)) {
                    assert(node.argTypes[i] != HIRTypeRef());
                    desTy = &node.argTypes[i];
                }

                checkTypesEqual(*desTy, node.mArgs[i]);
            }

            for (auto& val : node.mArgs) {
                val->visit(*this);
            }
        }

        void visit(HIRExprNodeStructLiteral& node) override {
            TRACE_FUNCTION_F(&node << " " << node.realPath << "{...} [" << (node.isStruct ? "struct" : "enum") << "]");
            const auto& sp = node.span();
            if (node.baseValue) {
                checkTypesEqual(node.baseValue->span(), node.resType, node.baseValue->resType);
            }
            const auto& tyPath = node.realPath;

            // - Create ivars in path, and set result type
            const auto& ty = node.resType;
            ASSERT_BUG(sp, ty->is_Path(), "Result type of _StructLiteral isn't Path");

            const tStructFields* fieldsPtr = nullptr;
            TU_MATCH_HDRA( (ty->as_Path().binding), {)
            TU_ARMA(Unbound, e) {
                }
                TU_ARMA(Opaque, e) {
                }
                TU_ARMA(Enum, e) {
                    const auto& varName = tyPath.mPath.components().back();
                    const auto& enm = *e;
                    auto idx = enm.findVariant(varName);
                    ASSERT_BUG(sp, idx != SIZE_MAX, "");
                    ASSERT_BUG(sp, enm.mData.is_Data(), "");
                    const auto& var = enm.mData.as_Data()[idx];

                    const auto& str = *var.type->as_Path().binding.as_Struct();
                    ASSERT_BUG(sp, var.isStruct, "Struct literal for enum on non-struct variant");
                    fieldsPtr = &str.mData.as_Named();
                }
                TU_ARMA(Union, e) {
                    fieldsPtr = &e->mVariants;
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

                    ASSERT_BUG(node.span(), e->mData.is_Named(), "StructLiteral not pointing to a braced struct, instead " << e->mData.tagStr() << " - " << ty);
                    fieldsPtr = &e->mData.as_Named();
                }
            }
            ASSERT_BUG(node.span(), fieldsPtr, "Didn't get field for path in _StructLiteral - " << ty);
            const tStructFields& fields = *fieldsPtr;
            for(const auto& fld : fields) {
                DEBUG(fld.name << ": " << fld.ty);
            }

            auto ms = MonomorphStatePtr(mResolve.crate.types, ty, &tyPath.mParams, nullptr);

            // Bind fields with type params (coercable)
            for( auto& val : node.values)
            {
                const auto& name = val.first;
                auto it = ::std::find_if(fields.begin(), fields.end(), [&](const HIRStructField& v) -> bool {
                    return v.name == name;
                });
                assert(it != fields.end());
                const auto& desTyR = it->ty;
                auto& desTyCache = node.valueTypes[it - fields.begin()];
                const auto* desTy = &desTyR;

                DEBUG(name << " : " << desTyR);
                if (monomorphiseTypeNeeded(desTyR)) {
                    ASSERT_BUG(node.span(), desTyCache != HIRTypeRef(), "Type " << desTyR << " needs monomorph, but isn't in cache: Field " << name);
                    desTyCache = ms.monomorphType(node.span(), desTyR);
                    mResolve.expandAssociatedTypes(node.span(), desTyCache);
                    desTy = &desTyCache;
                }
                DEBUG("." << name << " : " << *desTy);
                checkTypesEqual(*desTy, val.second);
            }

            for( auto& val : node.values ) {
                val.second->visit(*this);
            }
            if( node.baseValue ) {
                node.baseValue->visit(*this);
            }
        }

        void visit(HIRExprNodeUnitVariant& node) override {
            TRACE_FUNCTION_F(&node << " " << node.mPath << " [" << (node.isStruct ? "struct" : "enum") << "]");
            const auto& sp = node.span();
            const auto& ty = node.resType;
            ASSERT_BUG(sp, ty->is_Path(), "Result type of _UnitVariant isn't Path");

            TU_MATCH(
                HIRTypePathBinding,
                (ty->as_Path().binding),
                (e),
                (Unbound, ),
                (Opaque, ),
                (
                    Enum, const auto& varName = node.mPath.mPath.components().back(); const auto& enm = *e; if (const auto* e = enm.mData.opt_Data()) {
                        auto idx = enm.findVariant(varName);
                        ASSERT_BUG(sp, idx != SIZE_MAX, "");
                        ASSERT_BUG(sp, (*e)[idx].type == mResolve.crate.types.unit(), "");
                    }
                ),
                (Union, BUG(sp, "Union with _UnitVariant");),
                (ExternType, BUG(sp, "ExternType with _UnitVariant");),
                (Struct, assert(e->mData.is_Unit());)
            )
        }

        void checkFunction(const Span& sp, const HIRPath& path, HIRExprCallCache& cache) {
            // Do function resolution again, this time with concrete types.
            const HIRFunction* fcnPtr = nullptr;
            MonomorphStatePtr monomorphCb(mResolve.crate.types);

            TU_MATCH_HDRA( (path.mData), {)
            TU_ARMA(Generic, e) {
                    const auto& pathParams = e.mParams;

                    const auto& fcn = mResolve.crate.getFunctionByPath(sp, e.mPath);
                    fcnPtr = &fcn;
                    cache.fcnParams = &fcn.mParams;

                    monomorphCb = MonomorphStatePtr(mResolve.crate.types, nullptr, nullptr, &pathParams);
                }
                TU_ARMA(UfcsKnown, e) {
                    const auto& traitParams = e.trait.mParams;
                    const auto& pathParams = e.params;

                    const auto& trait = mResolve.crate.getTraitByPath(sp, e.trait.mPath);
                    if (trait.values.count(e.item) == 0) {
                        BUG(sp, "Method '" << e.item << "' of trait " << e.trait.mPath << " doesn't exist");
                    }

                    const auto& fcn = trait.values.at(e.item).as_Function();
                    cache.fcnParams = &fcn.mParams;
                    cache.topParams = &trait.mParams;

                    // Add a bound requiring the Self type impl the trait
                    checkTraitBound(sp, e.trait.mPath, e.trait.mParams, e.type);

                    fcnPtr = &fcn;

                    monomorphCb = MonomorphStatePtr(mResolve.crate.types, e.type, &traitParams, &pathParams);
                }
                TU_ARMA(UfcsUnknown, e) {
                    TODO(sp, "Hit a UfcsUnknown (" << path << ") - Is this an error?");
                }
                TU_ARMA(UfcsInherent, e) {
                    // - Locate function (and impl block)
                    const HIRTypeImpl* implPtr = nullptr;
                    mResolve.crate.findTypeImpls(e.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
                        DEBUG("- impl" << impl.mParams.fmtArgs() << " " << impl.mType);
                        auto it = impl.methods.find(e.item);
                        if (it == impl.methods.end()) {
                            return false;
                        }
                        fcnPtr = &it->second.data;
                        implPtr = &impl;
                        return true;
                    });
                    if (!fcnPtr) {
                        ERROR(sp, E0000, "Failed to locate function " << path);
                    }
                    assert(implPtr);

                    cache.fcnParams = &fcnPtr->mParams;

                    // NOTE: Trusts the existing cache.
                    ASSERT_BUG(sp, e.implParams.types.size() == implPtr->mParams.types.size(), "Path impl_params cache is missized - " << e.implParams.types.size() << " != " << implPtr->mParams.types.size());
                    auto& implParams = e.implParams;

                    // Create monomorphise callback
                    const auto& fcnParams = e.params;
                    monomorphCb = MonomorphStatePtr(mResolve.crate.types, e.type, &implParams, &fcnParams);
                }
            }

            assert( fcnPtr );
            const auto& fcn = *fcnPtr;
            monomorphCb.setConstevalState(mResolve.crate, HIRItemPath(path));

            // --- Monomorphise the argument/return types (into current context)
            cache.argTypes.clear();
            for(const auto& arg : fcn.mArgs) {
                DEBUG("Arg " << arg.first << ": " << arg.second);
                cache.argTypes.push_back(monomorphCb.monomorphType(sp, arg.second, false));
                mResolve.expandAssociatedTypes(sp, cache.argTypes.back());
                DEBUG("= " << cache.argTypes.back());
            }
            DEBUG("Ret " << fcn.returnType);
            // Replace ErasedType and monomorphise
            cache.argTypes.push_back( monomorphCb.monomorphType(sp, fcn.returnType, false) );
            rewriteTyWith(mResolve.crate.types, cache.argTypes.back(), [&](HIRTypeRef& ty, HIRTypeData&)->bool {
                if (this->expandErasedTypes && ty->is_ErasedType() && ty->as_ErasedType().inner.is_Fcn()) {
                    const auto& e = ty->as_ErasedType().inner.as_Fcn();

                    // Check the origin, because monomorph might end up introducing other erased types
                    if (e.origin == path) {
                        ASSERT_BUG(sp, e.index < fcnPtr->mCode.erasedTypes.size(), "");
                        const auto& erasedTypeReplacement = fcnPtr->mCode.erasedTypes.at(e.index);
                        ty = monomorphCb.monomorphType(sp, erasedTypeReplacement, false);
                        return true;
                    }
                }
                return false;
                });
            mResolve.expandAssociatedTypes(sp, cache.argTypes.back());
            DEBUG("= " << cache.argTypes.back());

            cache.monomorph.reset( new MonomorphStatePtr(monomorphCb) );

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
                        HIRGenericParams emptyHrtb;
                        auto _ = cache.monomorph->pushHrb(be.hrtbs ? *be.hrtbs : emptyHrtb);
                        DEBUG("Bound " << be.type << ":  " << be.trait);
                        auto realType = cache.monomorph->monomorphType(sp, be.type);
                        mResolve.expandAssociatedTypes(sp, realType);
                        auto realTrait = cache.monomorph->monomorphTraitpath(sp, be.trait, false);
                        mResolve.expandAssociatedTypesTp(sp, realTrait);
                        DEBUG("= (" << realType << ": " << realTrait << ")");
                        const auto& traitParams = realTrait.mPath.mParams;

                        const auto& traitPath = be.trait.mPath.mPath;
                        checkTraitBound(sp, traitPath, traitParams, realType);

                        // TODO: Either - Don't include the above impl bound, or change the below trait to the one that has that type
                        for (auto& assoc : realTrait.typeBounds) {
                            HIRGenericPath typeTraitPath;
                            bool hasTy = mResolve.traitContainsType(sp, realTrait.mPath, *be.trait.traitPtr, assoc.first.c_str(), typeTraitPath);
                            ASSERT_BUG(sp, hasTy, "Type " << assoc.first << " not found in chain of " << realTrait.mPath);

                            checkAssociatedType(sp, assoc.second.type, typeTraitPath.mPath, typeTraitPath.mParams, realType, assoc.first.c_str());
                        }
                    }
                    TU_ARMA(TypeEquality, be) {
                        auto realTypeLeft = cache.monomorph->monomorphType(sp, be.type);
                        auto realTypeRight = cache.monomorph->monomorphType(sp, be.otherType);
                        mResolve.expandAssociatedTypes(sp, realTypeLeft);
                        mResolve.expandAssociatedTypes(sp, realTypeRight);
                        checkTypesEqual(sp, realTypeLeft, realTypeRight);
                    }
                }
            }
        }

        void visit(HIRExprNodeCallPath& node) override {
            const auto& sp = node.span();
            TRACE_FUNCTION_F(&node << " " << node.mPath << "(..., )");

            for (auto& val : node.mArgs) {
                val->visit(*this);
            }

            checkFunction(sp, node.mPath, node.cache);

            // Check types
            for (unsigned int i = 0; i < node.cache.argTypes.size() - 1; i++) {
                DEBUG("CHECK ARG " << i << " " << node.cache.argTypes[i] << " == " << node.mArgs[i]->resType);
                checkTypesEqual(sp, node.cache.argTypes[i], node.mArgs[i]->resType);
            }
            for (unsigned int i = node.cache.argTypes.size() - 1; i < node.mArgs.size(); i++) {
                DEBUG("CHECK ARG " << i << " *  == " << node.mArgs[i]->resType);
                // TODO: Check that the types here are valid.
            }
            DEBUG("CHECK RV " << node.resType << " == " << node.cache.argTypes.back());
            checkTypesEqual(sp, node.resType, node.cache.argTypes.back());
        }

        void visit(HIRExprNodeCallValue& node) override {
            TRACE_FUNCTION_F(&node << " (...)(..., )");

            const auto& valTy = node.mValue->resType;

            if (valTy->is_Function() || valTy->is_NamedFunction()) {
                DEBUG("- Function pointer: " << valTy);
                HIRTypeRef tmpFt;
                const auto* e = valTy->opt_Function();
                if (!e) {
                    tmpFt = mResolve.crate.types.function(valTy->as_NamedFunction().decay(mResolve.crate.types, node.span()));
                    mResolve.expandAssociatedTypes(node.span(), tmpFt);
                    e = &tmpFt->as_Function();
                }
                auto hrls = e->hrls.makeEmptyParams(true);
                auto m = MonomorphHrlsOnly(mResolve.crate.types, hrls);
                if (e->isVariadic ? node.mArgs.size() < e->argTypes.size() : node.mArgs.size() != e->argTypes.size()) {
                    ERROR(node.span(), E0000, "Incorrect number of arguments to call via " << valTy);
                }
                for (unsigned int i = 0; i < e->argTypes.size(); i++) {
                    checkTypesEqual(node.mArgs[i]->span(), m.monomorphType(node.span(), e->argTypes[i]), node.mArgs[i]->resType);
                }
                checkTypesEqual(node.span(), node.resType, m.monomorphType(node.span(), e->mRettype));
            } else if (node.traitUsed == HIRExprNodeCallValue::TraitUsed::Unknown) {
            } else {
                // 1. Look up the encoded trait
                const HIRSimplePath* traitP;
                switch (node.traitUsed) {
                    case HIRExprNodeCallValue::TraitUsed::Fn:
                        traitP = &mResolve.crate.getLangItemPath(node.span(), "fn");
                        break;
                    case HIRExprNodeCallValue::TraitUsed::FnMut:
                        traitP = &mResolve.crate.getLangItemPath(node.span(), "fn_mut");
                        break;
                    case HIRExprNodeCallValue::TraitUsed::FnOnce:
                        traitP = &mResolve.crate.getLangItemPath(node.span(), "fn_once");
                        break;
                    default:
                        throw "";
                }
                const auto& trait = *traitP;

                ::std::vector<HIRTypeRef> tupEnts;
                for (const auto& arg : node.mArgs) {
                    tupEnts.push_back(arg->resType);
                }
                HIRPathParams params;
                params.types.push_back(mResolve.crate.types.tuple(mv$(tupEnts)));

                bool found = mResolve.findImpl(node.span(), trait, &params, valTy, [&](auto, bool fuzzy) -> bool {
                    ASSERT_BUG(node.span(), !fuzzy, "Fuzzy match in check pass");
                    return true;
                });
                if (!found) {
                    ERROR(node.span(), E0000, "Unable to find a matching impl of " << trait << " for " << valTy);
                }
                auto expRet = mResolve.crate.types.path(HIRPath(node.mValue->resType, {mResolve.crate.getLangItemPath(node.span(), "fn_once"), mv$(params)}, "Output", {}), {});
                mResolve.expandAssociatedTypes(node.span(), expRet);
                checkTypesEqual(node.span(), node.resType, expRet);
            }

            node.mValue->visit(*this);
            for (auto& val : node.mArgs) {
                val->visit(*this);
            }
        }

        void visit(HIRExprNodeCallMethod& node) override {
            TRACE_FUNCTION_F(&node << " (...)." << node.method << "(...,) - " << node.methodPath);

            node.mValue->visit(*this);
            for (auto& val : node.mArgs) {
                val->visit(*this);
            }

            const Span& sp = node.span();
            checkFunction(sp, node.methodPath, node.cache);

            // Check types
            for (unsigned int i = 0; i < node.cache.argTypes.size() - 2; i++) {
                DEBUG("CHECK ARG " << i << " " << node.cache.argTypes[1 + i] << " == " << node.mArgs[i]->resType);
                checkTypesEqual(sp, node.cache.argTypes[1 + i], node.mArgs[i]->resType);
            }
            for (unsigned int i = node.cache.argTypes.size() - 1; i < node.mArgs.size(); i++) {
                DEBUG("CHECK ARG " << i << " *  == " << node.mArgs[i]->resType);
                // TODO: Check that the types here are valid.
            }
            DEBUG("CHECK RV " << node.resType << " == " << node.cache.argTypes.back());
            checkTypesEqual(sp, node.resType, node.cache.argTypes.back());
        }

        void visit(HIRExprNodeField& node) override {
            TRACE_FUNCTION_F(&node << " (...)." << node.field);
            const auto& sp = node.span();
            const auto& strTy = node.mValue->resType;

            bool isIndex = ('0' <= node.field.c_str()[0] && node.field.c_str()[0] <= '9');
            if (strTy->is_Tuple()) {
                ASSERT_BUG(sp, isIndex, "Non-index _Field on tuple");
            } else if (strTy->is_NodeType()) {
                ASSERT_BUG(sp, isIndex, "Non-index _Field on magic type");
            } else {
                ASSERT_BUG(sp, strTy->is_Path(), "Value type of _Field isn't Path - " << strTy);
                const auto& tyE = strTy->as_Path();
                if (tyE.binding.is_Struct()) {
                    // TODO: Triple-check result, but that probably isn't needed
                } else if (tyE.binding.is_Union()) {
                } else {
                    ASSERT_BUG(sp, tyE.binding.is_Struct() || tyE.binding.is_Union(), "Value type of _Field isn't a Struct or Union - " << strTy);
                }
            }

            node.mValue->visit(*this);
        }

        void visit(HIRExprNodeTuple& node) override {
            TRACE_FUNCTION_F(&node << " (...,)");
            ASSERT_BUG(node.span(), node.resType->is_Tuple(), "Tuple literal didn't return tuple");
            const auto& tys = node.resType->as_Tuple();

            ASSERT_BUG(node.span(), tys.size() == node.vals.size(), "Bad element count in tuple literal - " << tys.size() << " != " << node.vals.size());
            for (unsigned int i = 0; i < node.vals.size(); i++) {
                checkTypesEqual(node.span(), tys[i], node.vals[i]->resType);
            }

            for (auto& val : node.vals) {
                val->visit(*this);
            }
        }

        void visit(HIRExprNodeArrayList& node) override {
            TRACE_FUNCTION_F(&node << " [...,]");
            // Cleanly equate into array (with coercions)
            const auto& innerTy = node.resType->as_Array().inner;
            for (auto& val : node.vals) {
                checkTypesEqual(val->span(), innerTy, val->resType);
            }

            for (auto& val : node.vals) {
                val->visit(*this);
            }
        }

        void visit(HIRExprNodeArraySized& node) override {
            TRACE_FUNCTION_F(&node << " [...; " << node.mSize << "]");

            const auto& innerTy = node.resType->as_Array().inner;
            checkTypesEqual(node.val->span(), innerTy, node.val->resType);

            node.val->visit(*this);
            //if(node.m_size.is_Unevaluated() && node.m_size.as_Unevaluated().is_Unevaluated())
            //{
            //    (*node.m_size.as_Unevaluated().as_Unevaluated())->visit( *this );
            //}
        }

        void visit(HIRExprNodeLiteral& node) override {
            // No validation needed
        }

        void visit(HIRExprNodePathValue& node) override {
            TRACE_FUNCTION_F(&node << " " << node.mPath);
            const Span& sp = node.span();

            MonomorphState outParams(mResolve.crate.types);
            StaticTraitResolve::ValuePtr v = this->mResolve.getValue(sp, node.mPath, outParams, /*signature_only=*/true);
            HIRTypeRef ty;
            TU_MATCH_HDRA( (v), {)
            TU_ARMA(NotFound, ve) {
                    BUG(sp, node.mPath << " Not found");
                }
                TU_ARMA(NotYetKnown, ve) {
                    // If the exact value can't be found, then
                    BUG(sp, node.mPath << " still unknown (has ivars?)");
                }
                TU_ARMA(Static, ve) {
                    ty = outParams.monomorphType(node.span(), ve->mType);
                    this->mResolve.expandAssociatedTypes(sp, ty);
                }
                TU_ARMA(Constant, ve) {
                    ty = outParams.monomorphType(node.span(), ve->mType);
                    this->mResolve.expandAssociatedTypes(sp, ty);
                }
                TU_ARMA(StructConstant, ve) {
                    // TODO: Check struct type
                }
                TU_ARMA(EnumValue, ve) {
                    // TODO: Check enum variant type
                }

                TU_ARMA(Function, ve) {
                    ty = mResolve.crate.types.intern(HIRTypeData::make_NamedFunction({node.mPath.clone(), ve}));
                }
                TU_ARMA(StructConstructor, ve) {
                    ty = mResolve.crate.types.intern(HIRTypeData::make_NamedFunction({node.mPath.clone(), ve.s}));
                }
                TU_ARMA(EnumConstructor, ve) {
                    ty = mResolve.crate.types.intern(HIRTypeData::make_NamedFunction({node.mPath.clone(), HIRTypeDataNamedFunctionTy::make_EnumConstructor({ve.e, ve.v})}));
                }
            }
            if( ty != HIRTypeRef() ) {
                checkTypesEqual(sp, node.resType, ty);
            }
        }

        void visit(HIRExprNodeVariable& node) override {
            // TODO: Check against variable slot? Nah.
        }

        void visit(HIRExprNodeConstParam& node) override {
            // TODO: Check against variable slot? Nah.
        }

        void visit(HIRExprNodeClosure& node) override {
            TRACE_FUNCTION_F(&node << " |...| ...");

            if (node.mCode) {
                checkTypesEqual(node.mCode->span(), node.returnType, node.mCode->resType);

                auto loops = ::std::move(this->loops);

                this->closureRetTypes.push_back(RetTarget(node.returnType));
                node.mCode->visit(*this);
                this->closureRetTypes.pop_back();

                this->loops = ::std::move(loops);
            }
        }

        void visit(HIRExprNodeGenerator& node) override {
            TRACE_FUNCTION_F(&node << " /*gen*/ |...| ...");

            if (node.mCode) {
                auto loops = ::std::move(this->loops);

                checkTypesEqual(node.mCode->span(), node.returnType, node.mCode->resType);
                this->closureRetTypes.push_back(RetTarget(node.returnType, node.yieldTy));
                node.mCode->visit(*this);
                this->closureRetTypes.pop_back();

                this->loops = ::std::move(loops);
            }
        }

        void visit(HIRExprNodeGeneratorWrapper& node) override {
            TRACE_FUNCTION_F(&node << " /*gen w*/ |...| ...");

            if (node.mCode) {
                auto loops = ::std::move(this->loops);

                checkTypesEqual(node.mCode->span(), node.returnType, node.mCode->resType);
                this->closureRetTypes.push_back(RetTarget(node.returnType, node.yieldTy));
                node.mCode->visit(*this);
                this->closureRetTypes.pop_back();

                this->loops = ::std::move(loops);
            }
        }

        void visit(HIRExprNodeAsyncBlock& node) override {
            TRACE_FUNCTION_F(&node << " async { ... }");

            // Can be null after generation
            if (node.mCode) {
                auto loops = ::std::move(this->loops);
                this->closureRetTypes.push_back(RetTarget(node.mCode->resType));
                node.mCode->visit(*this);
                this->closureRetTypes.pop_back();
                this->loops = ::std::move(loops);
            }
        }

    private:
        void checkTypesEqual(const HIRTypeData* l, const HIRExprNodeP& node) const {
            checkTypesEqual(node->span(), l, node->resType);
        }

        void checkTypesEqual(const Span& sp, const HIRTypeData* l, const HIRTypeData* r) const {
            struct Resolve: HIRResolvePlaceholders {
                HIRTypeInterner& types;
                mutable HIRTypeRef tmp;

                explicit Resolve(HIRTypeInterner& types)
                    : types(types)
                {
                }

                const HIRTypeData* getType(const Span& sp, const HIRTypeData* ty) const override {
                    if (const auto* e = ty->opt_ErasedType()) {
                        if (const auto* ee = e->inner.opt_Alias()) {
                            if (ee->inner->type != HIRTypeRef()) {
                                return tmp = MonomorphStatePtr(types, nullptr, &ee->params, nullptr).monomorphType(sp, ee->inner->type);
                            }
                        }
                    }
                    return ty;
                }

                const HIRConstGeneric& getVal(const Span& sp, const HIRConstGeneric& v) const override {
                    return v;
                }
            } getTypes(mResolve.crate.types);

            // TODO: Recurse when an erased type is encountered
            //if( const auto* e = l->opt_ErasedType() )
            //{
            //}
            //if( const auto* e = r->opt_ErasedType() )
            //{
            //}
            DEBUG(sp << " - " << l << " == " << r);
            MonomorphHrlsOnly(mResolve.crate.types, HIRPathParams()).monomorphType(sp, l);
            MonomorphHrlsOnly(mResolve.crate.types, HIRPathParams()).monomorphType(sp, r);
            if (/*l->is_Diverge() ||*/ r->is_Diverge()) {
                // Diverge, matches everything.
                // TODO: Is this always true?
            } else if (l->compareWithPlaceholders(sp, r, getTypes) != HIRCompare::Equal) {
                ERROR(sp, E0000, "Type mismatch\n - " << l << "\n!= " << r);
            } else {
                // All good
            }
        }

        void checkTraitBound(const Span& sp, const HIRSimplePath& trait, const HIRPathParams& params, const HIRTypeData* ity) const {
            DEBUG(sp << " - " << ity << " : " << trait << params);
            auto normalizedType = ity;
            auto normalizedParams = params.clone();
            mResolve.expandAssociatedTypes(sp, normalizedType);
            for (auto& type : normalizedParams.types) {
                mResolve.expandAssociatedTypes(sp, type);
            }
            const bool found = mResolve.findImpl(sp, trait, &normalizedParams, normalizedType, [](auto, bool) {
                return true;
            });
            if (!found) {
                ERROR(sp, E0000, "Cannot find an impl of " << trait << normalizedParams << " for " << normalizedType);
            }
        }

        void checkAssociatedType(
            const Span& sp,
            const HIRTypeData* res, // Expected result
            const HIRSimplePath& trait,
            const HIRPathParams& params,
            const HIRTypeData* ity,
            const char* name
            // TODO: Does this need params for the ATY??
        ) const {
            ASSERT_BUG(sp, name && name[0], "check_associated_type called without an associated type name");
            DEBUG(sp << " - " << res << " == < " << ity << " as " << trait << params << " >::" << name);
            bool found = mResolve.findImpl(sp, trait, &params, ity, [&](auto impl, bool fuzzy) {
                auto atyv = impl.getType(mResolve.crate.types, name, {});
                if (atyv == HIRTypeRef()) {
                    // TODO: Check that `res` is <ity as trait>::name
                } else {
                    mResolve.expandAssociatedTypes(sp, atyv);
                    if (res != atyv && !res->equalsIgnoringRegions(atyv)) {
                        ERROR(sp, E0000, "Associated type on " << trait << params << " for " << ity << " doesn't match - " << res << " != " << atyv);
                    }
                }

                return true;
            });
            if (!found) {
                ERROR(sp, E0000, "Cannot find an impl of " << trait << params << " for " << ity);
            }
        }

        void checkPattern(const HIRPattern& pat, const HIRTypeData* topTy) const {
            Span sp;
            TRACE_FUNCTION_F("pat=" << pat << " ty=" << topTy);
            const HIRTypeData* typ = topTy;
            // Implicit derefs
            for (size_t i = 0; i < pat.implicitDerefCount; i++) {
                typ = typ->as_Borrow().inner;
            }
            const HIRTypeData* ty = typ;

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
                    this->checkPatternValue(sp, pe.val, ty);
                }
                TU_ARMA(Range, pe) {
                    if (pe.start) {
                        this->checkPatternValue(sp, *pe.start, ty);
                    }
                    if (pe.end) {
                        this->checkPatternValue(sp, *pe.end, ty);
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
                        checkPattern(subpat, ty);
                    }
                }
            }
        }

        void checkPatternValue(const Span& sp, const HIRPattern::Value& pv, const HIRTypeData* ty) const {
            TU_MATCH_HDRA( (pv), { )
            TU_ARMA(Integer, e) {
                    if (e.type == HIRCoreType::Str) {
                    } else {
                        checkTypesEqual(sp, ty, mResolve.crate.types.primitive(e.type));
                    }
                }
                TU_ARMA(Float, e) {
                    if (e.type == HIRCoreType::Str) {
                    } else {
                        checkTypesEqual(sp, ty, mResolve.crate.types.primitive(e.type));
                    }
                }
                TU_ARMA(String, e) {
                    checkTypesEqual(sp, ty, mResolve.crate.types.borrow(HIRBorrowType::Shared, mResolve.crate.types.primitive(HIRCoreType::Str)));
                }
                TU_ARMA(ByteString, e) {
                    // Can either be a slice or an array
                }
                TU_ARMA(Named, e) {
                    MonomorphState ms(mResolve.crate.types);
                    auto v = mResolve.getValue(sp, e.path, ms, /*signature_only*/ true);
                    if (!v.is_Constant()) {
                        BUG(sp, "Pattern::Value::Named not a const - " << e.path);
                    }
                    HIRTypeRef tmp;
                    const auto& constTy = ms.maybeMonomorphType(sp, tmp, v.as_Constant()->mType);
                    checkTypesEqual(sp, ty, constTy);
                }
            }
        }

        const HIRSimplePath& getLangItemPath(const Span& sp, const char* name) const {
            return mResolve.crate.getLangItemPath(sp, name);
        }
    };

    class OuterVisitor: public HIRVisitor {
        StaticTraitResolve mResolve;

    public:
        OuterVisitor(const HIRCrate& crate)
            : HIRVisitor(nullptr, crate.types)
            , mResolve(crate)
        {
        }

        // NOTE: This is left here to ensure that any expressions that aren't handled by higher code cause a failure
        void visitExpr(HIRExprPtr& exp) override {
            BUG(Span(), "visit_expr hit in OuterVisitor");
        }

        void visitType(HIRTypeRef& ty) override {
            if (ty->is_Array()) {
                auto data = ty->cloneData();
                auto& e = data.as_Array();
                this->visitType(e.inner);
                DEBUG("Array size " << ty);
                if (auto* se1 = e.size.opt_Unevaluated()) {
                    if (auto* se = se1->opt_Unevaluated()) {
                        tArgs tmp;
                        auto tyUsize = mResolve.crate.types.primitive(HIRCoreType::Usize);
                        ExprVisitorValidate ev(mResolve, tmp, tyUsize);
                        ev.visitRoot(*(*se)->expr);
                    }
                }
                ty = mResolve.crate.types.intern(std::move(data));
            } else {
                HIRVisitor::visitType(ty);
            }
        }

        void visitConstgeneric(HIRConstGeneric& value) override {
            if (auto* unevaluated = value.opt_Unevaluated()) {
                tArgs tmp;
                auto& expr = *(**unevaluated).expr;
                ExprVisitorValidate ev(mResolve, tmp, expr->resType);
                ev.visitRoot(expr);
            }
        }

        // ------
        // Code-containing items
        // ------
        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            auto _ = this->mResolve.setItemGenerics(item.mParams);
            if (item.mCode) {
                DEBUG("Function code " << p);
                HIRTypeRef tmp;
                const auto& retTy = mResolve.fixTraitDefaultReturn(item.mCode->span(), p, item.returnType, tmp);
                ExprVisitorValidate ev(mResolve, item.mArgs, retTy);
                ev.visitRoot(item.mCode);
            } else {
                DEBUG("Function code " << p << " (none)");
            }
        }

        void visitStatic(HIRItemPath p, HIRStatic& item) override {
            auto _ = this->mResolve.setItemGenerics(item.mParams);
            if (item.mValue) {
                tArgs tmp;
                ExprVisitorValidate ev(mResolve, tmp, item.mType);
                ev.visitRoot(item.mValue);
            }
        }

        void visitConstant(HIRItemPath p, HIRConstant& item) override {
            auto _ = this->mResolve.setItemGenerics(item.mParams);
            if (item.mValue) {
                tArgs tmp;
                ExprVisitorValidate ev(mResolve, tmp, item.mType);
                ev.visitRoot(item.mValue);
            }
            mResolve.expandAssociatedTypes(Span(), item.mType);
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            auto _ = this->mResolve.setImplGenerics(MetadataType::None, item.mParams);

            HIRTypeRef enumType = mResolve.crate.types.primitive(HIREnum::getReprType(item.tagRepr));
            if (auto* e = item.mData.opt_Value()) {
                for (auto& var : e->variants) {
                    DEBUG("Enum value " << p << " - " << var.name);

                    if (var.expr) {
                        tArgs tmp;
                        ExprVisitorValidate ev(mResolve, tmp, enumType);
                        ev.visitRoot(var.expr);
                    }
                }
            }
        }

        void visitTrait(HIRItemPath p, HIRTrait& item) override {
            auto _ = this->mResolve.setImplGenerics(MetadataType::TraitObject, item.mParams);
            HIRVisitor::visitTrait(p, item);
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType);
            auto _ = this->mResolve.setImplGenerics(impl.mType, impl.mParams);

            HIRVisitor::visitTypeImpl(impl);
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << traitPath << " for " << impl.mType);
            auto _ = this->mResolve.setImplGenerics(impl.mType, impl.mParams);

            HIRVisitor::visitTraitImpl(traitPath, impl);
        }
    };
}

void TypecheckExpressionsValidateOne(const StaticTraitResolve& resolve, const ::std::vector<::std::pair<HIRPattern, HIRTypeRef>>& args, const HIRTypeData* retTy, const HIRExprPtr& code) {
    ExprVisitorValidate ev(resolve, args, retTy);
    ev.expandErasedTypes = false; // TODO: Make this an argument, we don't want to do this too early
    ev.visitRoot(const_cast<HIRExprPtr&>(code));
}

void TypecheckExpressionsValidate(HIRCrate& crate) {
    OuterVisitor ov(crate);
    ov.visitCrate(crate);
}

namespace {

    const HIRGenericParams& getParamsForItem(const Span& sp, const HIRCrate& crate, const HIRSimplePath& path, HIRVisitor::PathContext pc) {
        // Support for enum variants
        if (path.components().size() > 1) {
            const auto& pitem = crate.getTypeitemByPath(sp, path, false, true);
            if (pitem.is_Enum()) {
                return pitem.as_Enum().mParams;
            }
        }

        switch (pc) {
            case HIRVisitor::PathContext::VALUE: {
                const auto& item = crate.getValitemByPath(sp, path);

                TU_MATCH(
                    HIRValueItem,
                    (item),
                    (e),
                    (Import, BUG(sp, "Value path pointed to import - " << path << " = " << e.path);),
                    (Function, return e.mParams;),
                    (Constant, return e.mParams;),
                    (Static,
                     // TODO: Return an empty set?
                     BUG(sp, "Attepted to get parameters for static " << path);),
                    (StructConstructor, return getParamsForItem(sp, crate, e.ty, HIRVisitor::PathContext::TYPE);),
                    (StructConstant, return getParamsForItem(sp, crate, e.ty, HIRVisitor::PathContext::TYPE);)
                )
            } break;
            case HIRVisitor::PathContext::TRAIT:
                // TODO: treat PathContext::TRAIT differently
            case HIRVisitor::PathContext::TYPE: {
                const auto& item = crate.getTypeitemByPath(sp, path);

                TU_MATCH(HIRTypeItem, (item), (e), (Import, BUG(sp, "Type path pointed to import - " << path);), (TypeAlias, BUG(sp, "Type path pointed to type alias - " << path);), (TraitAlias, BUG(sp, "Type path pointed to trait alias - " << path);), (ExternType, static HIRGenericParams emptyParams; return emptyParams;), (Module, BUG(sp, "Type path pointed to module - " << path);), (Struct, return e.mParams;), (Enum, return e.mParams;), (Union, return e.mParams;), (Trait, return e.mParams;))
            } break;
        }
        throw "";
    }

    class Visitor: public HIRVisitor {
        HIRCrate& crate;
        StaticTraitResolve mResolve;

        const HIRTrait* currentTrait = nullptr;
        const HIRItemPath* mCurrentTraitPath = nullptr;

        HIRGenericParams* curParams = nullptr;
        unsigned curParamsLevel = 0;
        HIRItemPath* fcnPath = nullptr;
        HIRFunction* fcnPtr = nullptr;
        unsigned int fcnErasedCount = 0;

        ::std::vector<const HIRTypeData*> selfTypes;
        ::std::vector<HIRLifetimeRef*> currentLifetime;

        typedef ::std::vector<::std::pair<const HIRSimplePath*, const HIRTrait*>> tTraitImports;
        tTraitImports traits;

    public:
        Visitor(HIRCrate& crate)
            : HIRVisitor(nullptr, crate.types)
            , crate(crate)
            , mResolve(crate)
        {
        }

    private:
        struct ModTraitsGuard {
            Visitor* v;
            tTraitImports oldImports;

            ~ModTraitsGuard() {
                this->v->traits = mv$(this->oldImports);
            }
        };

        ModTraitsGuard pushModTraits(const HIRModule& mod) {
            static Span sp;
            DEBUG("");
            auto rv = ModTraitsGuard{this, mv$(this->traits)};
            for (const auto& traitPath : mod.traits) {
                DEBUG("- " << traitPath);
                traits.push_back(::std::make_pair(&traitPath, &this->crate.getTraitByPath(sp, traitPath)));
            }
            return rv;
        }

        void checkParameters(const Span& sp, const HIRGenericParams& paramDef, HIRPathParams& paramVals) {
            MonomorphStatePtr ms(crate.types, selfTypes.empty() ? nullptr : selfTypes.back(), &paramVals, nullptr);

            if (paramVals.mLifetimes.size() == 0) {
                paramVals.mLifetimes.resize(paramDef.mLifetimes.size());
            }
            if (paramVals.mLifetimes.size() != paramDef.mLifetimes.size()) {
                ERROR(sp, E0000, "Incorrect lifetime param count, expected " << paramDef.mLifetimes.size() << ", got " << paramVals.mLifetimes.size());
            }

            while (paramVals.types.size() < paramDef.types.size()) {
                unsigned int i = paramVals.types.size();
                const auto& tyDef = paramDef.types[i];
                if (tyDef.defaultValue->is_Infer()) {
                    ERROR(sp, E0000, "Unspecified parameter with no default - " << paramDef.fmtArgs() << " with " << paramVals);
                }

                // Replace and expand
                paramVals.types.push_back(ms.monomorphType(sp, tyDef.defaultValue));
                DEBUG("Add missing param (using default): " << paramVals.types.back());
            }

            if (paramVals.types.size() != paramDef.types.size()) {
                ERROR(sp, E0000, "Incorrect number of parameters - expected " << paramDef.types.size() << ", got " << paramVals.types.size());
            }

            for (unsigned int i = 0; i < paramVals.types.size(); i++) {
                if (paramVals.types[i] == HIRTypeRef()) {
                    // TODO: Why is this pulling in the default? Why not just leave it as-is

                    //if( param_def.m_types[i].m_default == ::HIR::TypeRef() )
                    // TODO: Monomorphise?
                    paramVals.types[i] = ms.monomorphType(sp, paramDef.types[i].defaultValue);
                    DEBUG("Update `_` param (using default): " << paramDef.types[i].defaultValue << " -> " << paramVals.types[i]);
                }
            }

            // TODO: Check generic bounds
            for (const auto& bound : paramDef.bounds) {
                TU_MATCH(
                    HIRGenericBound,
                    (bound),
                    (e),
                    (Lifetime, ),
                    (TypeLifetime, ),
                    (
                        TraitBound,
                        // TODO: Check for an implementation of this trait
                        DEBUG("TODO: Check bound " << e.type << " : " << e.trait.mPath);
                    ),
                    (TypeEquality,
                     // TODO: Check that two types are equal in this case
                     DEBUG("TODO: Check equality bound " << e.type << " == " << e.otherType);)
                )
            }
        }

    public:
        void visitLifetime(const Span& sp, HIRLifetimeRef& lft) {
            if (!lft.isParam()) {
                switch (lft.binding) {
                    case HIRLifetimeRef::STATIC: // 'static
                        break;
                    case HIRLifetimeRef::INFER: // '_
                        //TODO(sp, "Handle explicitly elided lifetimes");
                    case HIRLifetimeRef::UNKNOWN: // <none>
                        // If there's a current liftime (i.e. we're within a borrow), then use that
                        if (!currentLifetime.empty() && currentLifetime.back()) {
                            lft = *currentLifetime.back();
                        }
                        // Otherwise, try to make a new one
                        else if (curParams) {
                            auto idx = curParams->mLifetimes.size();
                            curParams->mLifetimes.push_back(HIRLifetimeDef{RcString::newInterned(FMT("elided#" << idx))});
                            lft.binding = curParamsLevel * 256 + idx;
                        } else {
                            // TODO: Would error here, but don't fully support HKTs (e.g. `Fn(&i32)`)
                        }
                        break;
                    default:
                        BUG(sp, "Unexpected lifetime binding - " << lft);
                }
            }
        }

        void visitPathParams(HIRPathParams& pp) override {
            static Span _sp;
            const Span& sp = _sp;

            for (auto& lft : pp.mLifetimes) {
                visitLifetime(sp, lft);
            }

            HIRVisitor::visitPathParams(pp);
        }

        void visitType(HIRTypeRef& ty) override {
            static Span _sp;
            const Span& sp = _sp;

            assert(ty);
            auto data = ty->cloneData();

            // Lifetime elision logic!
            if (auto* e = data.opt_Borrow()) {
                visitLifetime(sp, e->lifetime);
                currentLifetime.push_back(&e->lifetime);
            }

            auto self = crate.types.self();
            if (data.is_ErasedType()) {
                selfTypes.push_back(self);
            }

            auto savedParams = std::make_pair(curParams, curParamsLevel);
            if (auto* e = data.opt_Function()) {
                curParams = &e->hrls;
                curParamsLevel = 3;
            }

            TU_MATCH_HDRA((data), {)
            TU_ARMA(Infer, e) {
                }
                TU_ARMA(Diverge, e) {
                }
                TU_ARMA(Primitive, e) {
                }
                TU_ARMA(Generic, e) {
                }
                TU_ARMA(Path, e) this->visitPath(e.path, HIRVisitor::PathContext::TYPE);
                TU_ARMA(TraitObject, e) {
                    if (e.mTrait.mPath != HIRSimplePath()) {
                        this->visitTraitPath(e.mTrait);
                    }
                    for (auto& marker : e.markers) {
                        this->visitGenericPath(marker, HIRVisitor::PathContext::TYPE);
                    }
                }
                TU_ARMA(ErasedType, e) {
                TU_MATCH_HDRA((e.inner), {)
                TU_ARMA(Known, inner) this->visitType(inner);
                        TU_ARMA(Alias, inner) this->visitPathParams(inner.params);
                        TU_ARMA(Fcn, inner) if (inner.origin != HIRSimplePath()) this->visitPath(inner.origin, HIRVisitor::PathContext::VALUE);
                }
                this->visitPathParams(e.use);
                for (auto& trait : e.traits) this->visitTraitPath(trait);
                }
                TU_ARMA(Array, e) {
                    this->visitType(e.inner);
                    if (auto* size = e.size.opt_Unevaluated()) {
                        this->visitConstgeneric(*size);
                    }
                }
                TU_ARMA(Slice, e) this->visitType(e.inner);
                TU_ARMA(Tuple, e) for (auto& inner : e) this->visitType(inner);
                TU_ARMA(Borrow, e) this->visitType(e.inner);
                TU_ARMA(Pointer, e) this->visitType(e.inner);
                TU_ARMA(NamedFunction, e) this->visitPath(e.path, HIRVisitor::PathContext::VALUE);
                TU_ARMA(Function, e) {
                    for (auto& arg : e.argTypes) {
                        this->visitType(arg);
                    }
                    this->visitType(e.mRettype);
                }
                TU_ARMA(NodeType, e) {
                }
            }

            curParams = savedParams.first;
            curParamsLevel = savedParams.second;

            if (data.is_ErasedType()) {
                selfTypes.pop_back();
            }

            if (data.is_Borrow()) {
                currentLifetime.pop_back();
            }


            if (auto* e = data.opt_TraitObject()) {
                visitLifetime(sp, e->lifetime);
            }

            if (auto* e = data.opt_ErasedType()) {
                for (auto& lft : e->lifetimeBounds) {
                    visitLifetime(sp, lft);
                }
            }

            ty = crate.types.intern(mv$(data));

            if (const auto* e = ty->opt_Path()) {
                TU_MATCH(HIRPath::Data, (e->path.mData), (pe), (Generic, ), (UfcsUnknown, TODO(sp, "Should UfcsKnown be encountered here?");), (UfcsInherent, TRACE_FUNCTION_FR("UfcsInherent - " << ty, ty); mResolve.expandAssociatedTypes(sp, ty);), (UfcsKnown, TRACE_FUNCTION_FR("UfcsKnown - " << ty, ty); mResolve.expandAssociatedTypes(sp, ty);))
            }
        }

        void visitGenericPath(HIRGenericPath& p, PathContext pc) override {
            static Span sp;
            TRACE_FUNCTION_F("p = " << p);
            const auto& params = getParamsForItem(sp, crate, p.mPath, pc);
            auto& args = p.mParams;

            checkParameters(sp, params, args);
            DEBUG("p = " << p);

            HIRVisitor::visitGenericPath(p, pc);
        }

    private:
        bool locateTraitItemInBounds(const Span& sp, HIRVisitor::PathContext pc, const HIRTypeData* tr, const HIRGenericParams& params, HIRPath::Data& pd) {
            for (const auto& b : params.bounds) {
                TU_IFLET(HIRGenericBound, b, TraitBound, e, DEBUG("- " << e.type << " : " << e.trait.mPath); if (e.type == tr) {
                    DEBUG(" - Match");
                    if (locateInTraitAndSet(sp, pc, e.trait.mPath, this->crate.getTraitByPath(sp, e.trait.mPath.mPath), pd)) {
                        return true;
                    }
                });
                // -
            }
            return false;
        }

        static HIRPath::Data getUfcsKnown(HIRPath::Data::Data_UfcsUnknown e, HIRGenericPath traitPath, const HIRTrait& trait) {
            return HIRPath::Data::make_UfcsKnown({mv$(e.type), mv$(traitPath), mv$(e.item), mv$(e.params)});
        }

        static bool locateItemInTrait(HIRVisitor::PathContext pc, const HIRTrait& trait, HIRPath::Data& pd) {
            const auto& e = pd.as_UfcsUnknown();

            switch (pc) {
                case HIRVisitor::PathContext::VALUE:
                    if (trait.values.find(e.item) != trait.values.end()) {
                        return true;
                    }
                    break;
                case HIRVisitor::PathContext::TRAIT:
                    break;
                case HIRVisitor::PathContext::TYPE:
                    if (trait.types.find(e.item) != trait.types.end()) {
                        return true;
                    }
                    break;
            }
            return false;
        }

        bool locateInTraitAndSet(const Span& sp, HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
            if (locateItemInTrait(pc, trait, pd)) {
                pd = getUfcsKnown(mv$(pd.as_UfcsUnknown()), makeGenericPath(traitPath.mPath, trait), trait);
                return true;
            }
            // Search all supertraits
            for (const auto& pt : trait.allParentTraits) {
                if (locateItemInTrait(pc, *pt.traitPtr, pd)) {
                    pd = getUfcsKnown(mv$(pd.as_UfcsUnknown()), makeGenericPath(traitPath.mPath, trait), trait);
                    return true;
                }
            }
            return false;
        }

        bool setFromImpl(const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            const auto& type = e.type;
            return this->crate.findTraitImpls(traitPath.mPath, type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
                DEBUG("FOUND impl" << impl.mParams.fmtArgs() << " " << traitPath.mPath << impl.traitArgs << " for " << impl.mType);
                // TODO: Check bounds
                for (const auto& bound : impl.mParams.bounds) {
                    DEBUG("- TODO: Bound " << bound);
                    return false;
                }
                pd = getUfcsKnown(mv$(e), makeGenericPath(traitPath.mPath, trait), trait);
                return true;
            });
        }

        bool locateInTraitImplAndSet(HIRVisitor::PathContext pc, const HIRGenericPath& traitPath, const HIRTrait& trait, HIRPath::Data& pd) {
            auto& e = pd.as_UfcsUnknown();
            if (this->locateItemInTrait(pc, trait, pd)) {
                return this->setFromImpl(traitPath, trait, pd);
            } else {
                DEBUG("- Item " << e.item << " not in trait " << traitPath.mPath);
            }

            // Search supertraits (recursively)
            for (const auto& pt : trait.allParentTraits) {
                if (this->locateItemInTrait(pc, *pt.traitPtr, pd)) {
                    // TODO: Monomorphise params?
                    return setFromImpl(pt.mPath, *pt.traitPtr, pd);
                } else {
                }
            }
            return false;
        }

        HIRGenericPath makeGenericPath(HIRSimplePath sp, const HIRTrait& trait) {
            auto traitPathG = HIRGenericPath(mv$(sp));
            for (unsigned int i = 0; i < trait.mParams.types.size(); i++) {
                traitPathG.mParams.types.push_back(crate.types.generic(trait.mParams.types[i].mName, i));
            }
            return traitPathG;
        }

        HIRGenericPath getCurrentTraitGp() const {
            assert(mCurrentTraitPath);
            assert(currentTrait);
            auto traitPath = HIRGenericPath(mCurrentTraitPath->getSimplePath());
            for (unsigned int i = 0; i < currentTrait->mParams.types.size(); i++) {
                traitPath.mParams.types.push_back(crate.types.generic(currentTrait->mParams.types[i].mName, i));
            }
            return traitPath;
        }

        void visitPathUfcsUnknown(const Span& sp, HIRPath& p, HIRVisitor::PathContext pc) {
            TRACE_FUNCTION_FR("UfcsUnknown - p=" << p, p);
            auto& e = p.mData.as_UfcsUnknown();

            this->visitType(e.type);
            this->visitPathParams(e.params);

            // Search for matching impls in current generic blocks
            if (mResolve.mItemGenerics != nullptr && locateTraitItemInBounds(sp, pc, e.type, *mResolve.mItemGenerics, p.mData)) {
                return;
            }
            if (mResolve.mImplGenerics != nullptr && locateTraitItemInBounds(sp, pc, e.type, *mResolve.mImplGenerics, p.mData)) {
                return;
            }

            if (const auto* te = e.type->opt_Generic()) {
                // If processing a trait, and the type is 'Self', search for the type/method on the trait
                // - TODO: This could be encoded by a `Self: Trait` bound in the generics, but that may have knock-on issues?
                if (te->name == "Self" && currentTrait) {
                    auto traitPath = this->getCurrentTraitGp();
                    if (this->locateInTraitAndSet(sp, pc, traitPath, *currentTrait, p.mData)) {
                        // Success!
                        return;
                    }
                }
                ERROR(sp, E0000, "Failed to find impl with '" << e.item << "' for " << e.type);
                return;
            } else {
                // 1. Search for applicable inherent methods (COMES FIRST!)
                if (this->crate.findTypeImpls(e.type, HIRResolvePlaceholdersNop(), [&](const auto& impl) {
                    DEBUG("- matched inherent impl " << e.type);
                    // Search for item in this block
                    switch (pc) {
                        case HIRVisitor::PathContext::VALUE:
                            if (impl.methods.find(e.item) == impl.methods.end()) {
                                return false;
                            }
                            // Found it, just keep going (don't care about details here)
                            break;
                        case HIRVisitor::PathContext::TRAIT:
                            return false;
                        case HIRVisitor::PathContext::TYPE:
                            if (impl.types.find(e.item) == impl.types.end()) {
                                return false;
                            }
                            break;
                    }

                    return true;
                })) {
                    auto newData = HIRPath::Data::make_UfcsInherent({mv$(e.type), mv$(e.item), mv$(e.params)});
                    p.mData = mv$(newData);
                    DEBUG("- Resolved, replace with " << p);
                    return;
                }
                // 2. Search all impls of in-scope traits for this method on this type
                for (const auto& traitInfo : traits) {
                    const auto& trait = *traitInfo.second;

                    switch (pc) {
                        case HIRVisitor::PathContext::VALUE:
                            if (trait.values.find(e.item) == trait.values.end()) {
                                continue;
                            }
                            break;
                        case HIRVisitor::PathContext::TRAIT:
                        case HIRVisitor::PathContext::TYPE:
                            if (trait.types.find(e.item) == trait.types.end()) {
                                continue;
                            }
                            break;
                    }
                    DEBUG("- Trying trait " << *traitInfo.first);

                    auto traitPath = HIRGenericPath(*traitInfo.first);
                    for (unsigned int i = 0; i < trait.mParams.types.size(); i++) {
                        traitPath.mParams.types.push_back(crate.types.infer());
                    }

                    // TODO: Search supertraits
                    // TODO: Should impls be searched first, or item names?
                    // - Item names add complexity, but impls are slower
                    if (this->locateInTraitImplAndSet(pc, mv$(traitPath), trait, p.mData)) {
                        return;
                    }
                }
            }

            // Couldn't find it
            ERROR(sp, E0000, "Failed to find impl with '" << e.item << "' for " << e.type << " (in " << p << ")");
        }

    public:
        void visitExpr(HIRExprPtr& exp) override {
            // No-op
        }

        void visitPath(HIRPath& p, HIRVisitor::PathContext pc) override {
            TU_MATCH(
                HIRPath::Data,
                (p.mData),
                (e),
                (Generic, this->visitGenericPath(e, pc);),
                (
                    UfcsKnown, this->visitType(e.type); selfTypes.push_back(e.type); this->visitGenericPath(e.trait, HIRVisitor::PathContext::TRAIT); selfTypes.pop_back();
                    // TODO: Locate impl block and check parameters
                ),
                (
                    UfcsInherent, this->visitType(e.type);
                    // TODO: Locate impl block and check parameters
                ),
                (UfcsUnknown, BUG(Span(), "Encountered unknown-trait UFCS path during outer typeck - " << p);)
            )
        }

        void visitParams(HIRGenericParams& params) override {
            TRACE_FUNCTION_F(params.fmtArgs());
            for (auto& tps : params.types) {
                this->visitType(tps.defaultValue);
            }

            for (auto& bound : params.bounds) {
                TU_MATCH_HDRA( (bound), {)
                TU_ARMA(Lifetime, e) {
                    }
                    TU_ARMA(TypeLifetime, e) {
                        this->visitType(e.type);
                    }
                    TU_ARMA(TraitBound, e) {
                        this->visitType(e.type);
                        selfTypes.push_back(e.type);
                        this->visitTraitPath(e.trait);
                        selfTypes.pop_back();
                    }
                    //(NotTrait, e) {
                    //    ::HIR::TypeRef  type;
                    //    ::HIR::GenricPath    trait;
                    //    }),
                    TU_ARMA(TypeEquality, e) {
                        this->visitType(e.type);
                        this->visitType(e.otherType);
                    }
                }
            }
        }

        void visitModule(HIRItemPath p, HIRModule& mod) override {
            auto _ = this->pushModTraits(mod);
            HIRVisitor::visitModule(p, mod);
        }

        void visitTrait(HIRItemPath p, HIRTrait& item) override {
            currentTrait = &item;
            mCurrentTraitPath = &p;

            auto _ = mResolve.setImplGenerics(MetadataType::TraitObject, item.mParams);
            auto self = crate.types.self();
            selfTypes.push_back(self);
            HIRVisitor::visitTrait(p, item);
            selfTypes.pop_back();

            currentTrait = nullptr;
        }

        void visitStruct(HIRItemPath p, HIRStruct& item) override {
            auto _ = mResolve.setImplGenerics(item.structMarkings.dstType, item.mParams);
            HIRVisitor::visitStruct(p, item);
        }

        void visitUnion(HIRItemPath p, HIRUnion& item) override {
            auto _ = mResolve.setImplGenerics(MetadataType::None, item.mParams);
            HIRVisitor::visitUnion(p, item);
        }

        void visitEnum(HIRItemPath p, HIREnum& item) override {
            auto _ = mResolve.setImplGenerics(MetadataType::None, item.mParams);
            HIRVisitor::visitEnum(p, item);
        }

        void visitAssociatedtype(HIRItemPath p, HIRAssociatedType& item) override {
            // Push `Self = <Self as CurTrait>::Type` for processing defaults in the bounds.
            auto pathAty = HIRPath(crate.types.self(), this->getCurrentTraitGp(), p.getName());
            auto tyAty = crate.types.path(mv$(pathAty), HIRTypePathBinding::make_Opaque({}));
            selfTypes.push_back(tyAty);

            HIRVisitor::visitAssociatedtype(p, item);

            selfTypes.pop_back();
        }

        void visitTypeAlias(HIRItemPath p, HIRTypeAlias& item) override {
            // Ignore type aliases, they don't have to typecheck.
        }

        void visitInherentType(HIRItemPath p, HIRTypeAlias& item) override {
            auto _ = mResolve.setItemGenerics(item.mParams);
            auto savedParams = std::make_pair(curParams, curParamsLevel);
            curParams = &item.mParams;
            curParamsLevel = 1;
            HIRVisitor::visitInherentType(p, item);
            curParams = savedParams.first;
            curParamsLevel = savedParams.second;
        }

        void addLifetimeBoundsForImplType(const Span& sp, HIRGenericParams& dst, const HIRTypeData* ty) {
            // REF: rustc-1.29.0-src/src/vendor/clap/src/args/arg.rs:54 - Omitted lifetime bounds

            // https://rust-lang.github.io/rfcs/2089-implied-bounds.html ?
            // HACK: Just grab the lifetime bounds from a path type
            if (ty->is_Path() && ty->as_Path().path.mData.is_Generic()) {
                const auto& gp = ty->as_Path().path.mData.as_Generic();
                const auto& ti = mResolve.crate.getTypeitemByPath(sp, gp.mPath);

                const HIRGenericParams* params = nullptr;
                if (const auto* e = ti.opt_Struct()) {
                    params = &e->mParams;
                } else if (const auto* e = ti.opt_Enum()) {
                    params = &e->mParams;
                } else if (const auto* e = ti.opt_Union()) {
                    params = &e->mParams;
                } else {
                    DEBUG("TODO: Obtain bounds from " << ti.tagStr());
                }

                if (params) {
                    MonomorphStatePtr ms(crate.types, nullptr, &gp.mParams, nullptr);
                    for (const auto& b : params->bounds) {
                        if (const auto* be = b.opt_Lifetime()) {
                            dst.bounds.push_back(HIRGenericBound::make_Lifetime({ms.monomorphLifetime(sp, be->test), ms.monomorphLifetime(sp, be->validFor)}));
                        }
                    }
                }
            }
        }

        void visitTypeImpl(HIRTypeImpl& impl) override {
            TRACE_FUNCTION_F("impl " << impl.mType);
            auto _ = mResolve.setImplGenerics(impl.mType, impl.mParams);
            selfTypes.push_back(impl.mType);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.mParams;
                curParamsLevel = 0;
                this->visitType(impl.mType);
                curParams = nullptr;
            }

            // Propagate bounds from the type
            addLifetimeBoundsForImplType(Span(), impl.mParams, impl.mType);

            HIRVisitor::visitTypeImpl(impl);
            // TODO: Check that the type is valid

            selfTypes.pop_back();
        }

        void visitTraitImpl(const HIRSimplePath& traitPath, HIRTraitImpl& impl) override {
            static Span sp;
            TRACE_FUNCTION_F("impl" << impl.mParams.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.mType);
            auto _ = mResolve.setImplGenerics(impl.mType, impl.mParams);
            selfTypes.push_back(impl.mType);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.mParams;
                curParamsLevel = 0;
                this->visitType(impl.mType);
                this->visitPathParams(impl.traitArgs);
                curParams = nullptr;
            }

            // Propagate bounds from the type
            addLifetimeBoundsForImplType(Span(), impl.mParams, impl.mType);

            HIRVisitor::visitTraitImpl(traitPath, impl);
            selfTypes.pop_back();

            // TODO: Check that the type+trait is valid
            // - And fix bad elided liftimes (match annotations if they were elided)
            {
                const auto& trait = mResolve.crate.getTraitByPath(sp, traitPath);
                for (auto& e : impl.methods) {
                    auto _ = mResolve.setItemGenerics(e.second.data.mParams);

                    const auto vIt = trait.values.find(e.first);
                    if (vIt == trait.values.end() || !vIt->second.is_Function()) {
                        ERROR(sp, E0000, "Trait " << traitPath << " doesn't have a method named " << e.first);
                    }
                    auto& implFcn = e.second.data;
                    const auto& traitFcn = vIt->second.as_Function();

                    auto fcnParams = traitFcn.mParams.makeNopParams(crate.types, 1);
                    MonomorphStatePtr ms{crate.types, impl.mType, &impl.traitArgs, &fcnParams};
                    HIRTypeRef tmp;
                    auto maybeMonomorph = [&](const HIRTypeData* ty) -> const HIRTypeData* {
                        if (monomorphiseTypeNeeded(ty)) {
                            tmp = ms.monomorphType(sp, ty);
                            mResolve.expandAssociatedTypes(sp, tmp);
                            return tmp;
                        } else {
                            return ty;
                        }
                    };

                    // Check signature
                    // - Includes fixing incorrectly elided lifetimes
                    // ```
                    // trait Foo<T> {
                    // }
                    // impl Foo<&Bar> for Baz {
                    //   fn foo(&self, bar: &Bar) { }
                    // }

                    std::vector<std::string> failures;
                    // -- Generics
                    if (implFcn.mParams.types.size() != traitFcn.mParams.types.size()) {
                        failures.push_back(FMT("Mismatched type param count (expected " << traitFcn.mParams.types.size() << ", got " << implFcn.mParams.types.size() << ")"));
                    }
                    // Different logic for lifetimes, only want to check un-elided lifetimes
                    // - Well, elided lifetimes can overlap non-elided ones (as long as they're identical)
                    if (implFcn.mParams.values.size() != traitFcn.mParams.values.size()) {
                        failures.push_back(FMT("Mismatched const param count (expected " << traitFcn.mParams.values.size() << ", got " << implFcn.mParams.values.size() << ")"));
                    }
                    // -- Arguments
                    if (implFcn.mArgs.size() != traitFcn.mArgs.size()) {
                        failures.push_back(FMT("Mismatched argument count (expected " << traitFcn.mArgs.size() << ", got " << implFcn.mArgs.size() << ")"));
                    }
                    if (implFcn.receiver != traitFcn.receiver) {
                        failures.push_back(FMT("Receiver type")); //"(expected " << trait_fcn.m_receiver << ", got " << impl_fcn.m_receiver));
                    }
                    for (size_t i = 0; i < std::min(implFcn.mArgs.size(), traitFcn.mArgs.size()); i++) {
                        if (!(i == 0 && (traitFcn.receiver == HIRFunction::Receiver::Free || implFcn.receiver == HIRFunction::Receiver::Free))) {
                            // Check the type.
                            // - Also, fix lifetime elision?
                            const auto& expTy = maybeMonomorph(traitFcn.mArgs[i].second);
                            /*const*/ auto& hasTy = implFcn.mArgs[i].second;

                            if (expTy != hasTy && !expTy->equalsIgnoringRegions(hasTy)) {
                                failures.push_back(FMT("Argument " << 1 + i << " mismatch - expected " << expTy << ", got " << hasTy));
                            }
                        }
                    }

                    // Handle `implTrait` in returns
                    // - Would need to re-create `exp_ret_ty` to keep the `impl Trait`, OR keep a non-erased/expanded copy of the type
                    // > The difference tends to be in lifetimes, so match the two types and update lifetimes?
                    struct MCB: public HIRMatchGenerics {
                        ::std::map<RcString, const HIRTypeData*> mapping;

                        HIRCompare cmpType(const Span& sp, const HIRTypeData* tyL, const HIRTypeData* tyR, tCbResolveType resolveCb) override {
                            // If the LHS is an ATY that starts with `erased#` then just accept it?
                            // - Also record the mapping
                            if (const auto* tyP = tyL->opt_Path()) {
                                if (const auto* pathP = tyP->path.mData.opt_UfcsKnown()) {
                                    if (pathP->item.compare(0, strlen(ATY_PREFIX_ERASED), ATY_PREFIX_ERASED) == 0) {
                                        mapping.insert(std::make_pair(pathP->item, tyR));
                                        return HIRCompare::Equal;
                                    }
                                }
                            }
                            return HIRMatchGenerics::cmpType(sp, tyL, tyR, resolveCb);
                        }

                        HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) override {
                            return (!ty->is_Generic() || ty->as_Generic() != g) ? HIRCompare::Unequal : HIRCompare::Equal;
                        }

                        HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) override {
                            return (!sz.is_Generic() || sz.as_Generic() != g) ? HIRCompare::Unequal : HIRCompare::Equal;
                        }
                    } matchCb;

                    const auto& expRetTy1 = maybeMonomorph(traitFcn.returnType);
                    if (!expRetTy1->matchTestGenerics(sp, implFcn.returnType, HIRResolvePlaceholdersNop(), matchCb)) {
                        failures.push_back(
                            FMT("Mismatched return type:\n"
                                << "  Expected " << expRetTy1 << "\n"
                                << "  Found    " << implFcn.returnType)
                        );
                    }
                    HIRTypeRef expRetTyReal;
                    const auto& expRetTy = matchCb.mapping.empty() ? expRetTy1 : (expRetTyReal = cloneTyWith(crate.types, sp, expRetTy1, [&](const HIRTypeData* ref, HIRTypeRef& out) -> bool {
                        if (const auto* tyP = ref->opt_Path()) {
                            if (const auto* pathP = tyP->path.mData.opt_UfcsKnown()) {
                                auto it = matchCb.mapping.find(pathP->item);
                                if (it != matchCb.mapping.end()) {
                                    out = it->second;
                                    return true;
                                }
                            }
                        }
                        return false;
                    }));

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
                                                 os << "    fn " << e.first << traitFcn.mParams.fmtArgs() << "(";
                                                 for (const auto& a : traitFcn.mArgs) {
                                                     os << a.first << ": " << maybeMonomorph(a.second) << ", ";
                                                 }
                                                 os << ")\n";
                                                 os << "    -> " << maybeMonomorph(traitFcn.returnType) << "\n";
                                                 os << "    " << traitFcn.mParams.fmtBounds();
                                             }
                                         )
                                      << "\n"
                                      << "Impl :\n"
                                      << FMT_CB(
                                             os,
                                             {
                                                 os << "    fn " << e.first << implFcn.mParams.fmtArgs() << "(";
                                                 for (const auto& a : implFcn.mArgs) {
                                                     os << a.first << ": " << a.second << ", ";
                                                 }
                                                 os << ")\n";
                                                 os << "    -> " << implFcn.returnType << "\n";
                                                 os << "    " << implFcn.mParams.fmtBounds();
                                             }
                                         )
                                      << "\n"
                                      << "in impl" << impl.mParams.fmtArgs() << " " << traitPath << impl.traitArgs << " for " << impl.mType
                        );
                    }
                    // HACK: Replace all types (which should be functionally identical) so lifetimes match
                    // - This is needed for monomorphisation to work properly?
                    // REF: rustc-1.29.0/src/vendor/serde/src/private/de.rs:1379
                    // Counter-ref: rustc-1.54.0
                    // Update AFTER the checks
                    DEBUG("Replace generic block's lifetimes with " << traitFcn.mParams.fmtArgs());
                    implFcn.mParams.mLifetimes = traitFcn.mParams.mLifetimes;
                    // Replace the lifetime bounds too (undoes some potential confusion from elision)
                    {
                        auto& bl = implFcn.mParams.bounds;
                        bl.erase(
                            std::remove_if(
                                bl.begin(),
                                bl.end(),
                                [](const HIRGenericBound& b) {
                            return b.is_Lifetime();
                        }
                            ),
                            bl.end()
                        );
                    }
                    for (const auto& b : traitFcn.mParams.bounds) {
                        TU_MATCH_HDRA( (b), { )
                        default:
                            break;
                            TU_ARMA(TypeLifetime, be) {
                                implFcn.mParams.bounds.push_back(HIRGenericBound::make_TypeLifetime({ms.monomorphType(sp, be.type), ms.monomorphLifetime(sp, be.validFor)}));
                            }
                            TU_ARMA(Lifetime, be) {
                                implFcn.mParams.bounds.push_back(HIRGenericBound::make_Lifetime({ms.monomorphLifetime(sp, be.test), ms.monomorphLifetime(sp, be.validFor)}));
                            }
                        }
                    }

                    // HACK: Clone the expected type, so the lifetimes match.
                    DEBUG("Updating < " << impl.mType << " as " << traitPath << impl.traitArgs << " >::" << e.first);
                    implFcn.returnType = expRetTy;
                    for (size_t i = 0; i < std::min(implFcn.mArgs.size(), traitFcn.mArgs.size()); i++) {
                        DEBUG("ARG" << i << "> " << traitFcn.mArgs[i].second);
                        implFcn.mArgs[i].second = mResolve.monomorphExpand(sp, traitFcn.mArgs[i].second, ms);
                    }
                    DEBUG("Updated < " << impl.mType << " as " << traitPath << impl.traitArgs << " >::" << e.first);

                    DEBUG(FMT_CB(os, {
                        os << "fn " << e.first << implFcn.mParams.fmtArgs() << "(";
                        for (const auto& a : implFcn.mArgs) {
                            os << a.first << ": " << a.second << ", ";
                        }
                        os << ")";
                        os << implFcn.mParams.fmtBounds();
                    }));
                }
                for (const auto& e : impl.constants) {
                    const auto& vi = trait.values.at(e.first);
                    if (!vi.is_Constant()) {
                        ERROR(sp, E0000, "Trait " << traitPath << " doesn't have a constant named " << e.first);
                    }
                    const auto& implConst = e.second.data;
                    const auto& traitConst = vi.as_Constant();

                    // Check type
                }
                for (const auto& e : impl.statics) {
                    const auto& vi = trait.values.at(e.first);
                    if (!vi.is_Static()) {
                        ERROR(sp, E0000, "Trait " << traitPath << " doesn't have a static named " << e.first);
                    }
                    const auto& implStatic = e.second.data;
                    const auto& traitStatic = vi.as_Static();

                    // Check type
                }
                for (const auto& e : trait.types) {
                    const auto& traitType = trait.types.at(e.first);
                    const auto& implType = e.second;

                    // Check that the bounds fit
                }
            }
        }

        void visitMarkerImpl(const HIRSimplePath& traitPath, HIRMarkerImpl& impl) override {
            TRACE_FUNCTION_F("impl " << traitPath << " for " << impl.mType << " { }");
            auto _ = mResolve.setImplGenerics(impl.mType, impl.mParams);
            selfTypes.push_back(impl.mType);

            // Pre-visit so lifetime elision can work
            {
                curParams = &impl.mParams;
                curParamsLevel = 0;
                this->visitType(impl.mType);
                this->visitPathParams(impl.traitArgs);
                curParams = nullptr;
            }

            // Propagate bounds from the type/trait
            addLifetimeBoundsForImplType(Span(), impl.mParams, impl.mType);

            HIRVisitor::visitMarkerImpl(traitPath, impl);
            // TODO: Check that the type+trait is valid

            selfTypes.pop_back();
        }

        void visitFunction(HIRItemPath p, HIRFunction& item) override {
            TRACE_FUNCTION_F(p);

            if (mResolve.crate.getLangItemPathOpt("sized").components().empty()) {
                ERROR(Span(), E0000, "requires `sized` lang_item");
            }

            auto _ = mResolve.setItemGenerics(item.mParams);
            // NOTE: Superfluous... except that it makes the params valid for the return type.
            visitParams(item.mParams);

            fcnPtr = &item;
            auto firstElidedLifetimeIdx = item.mParams.mLifetimes.size();

            // Visit arguments
            // - Used to convert `impl Trait` in argument position into generics
            // - Done first so the path in return-position `impl Trait` is valid
            curParams = &item.mParams;
            curParamsLevel = 1;
            for (auto& arg : item.mArgs) {
                TRACE_FUNCTION_F("ARG " << arg);
                visitType(arg.second);
            }
            curParams = nullptr;

            // Get output lifetime
            // - Try `&self`'s lifetime (if it was an elided lifetime)
            HIRLifetimeRef elidedOutputLifetime;
            if (item.receiver != HIRFunction::Receiver::Free) {
                if (const auto* b = item.mArgs[0].second->opt_Borrow()) {
                    // If this was an elided lifetime.
                    if (b->lifetime.isParam() && (b->lifetime.binding >> 8) == 1 && (b->lifetime.binding & 0xFF) > firstElidedLifetimeIdx) {
                        elidedOutputLifetime = b->lifetime;
                    }
                }
            }
            // - OR, look for only one elided lifetime
            if (elidedOutputLifetime == HIRLifetimeRef()) {
                if (item.mParams.mLifetimes.size() == firstElidedLifetimeIdx + 1) {
                    elidedOutputLifetime = HIRLifetimeRef(256 + firstElidedLifetimeIdx);
                }
            }
            // If present, set it (push to the stack)
            assert(currentLifetime.empty());
            if (elidedOutputLifetime != HIRLifetimeRef()) {
                currentLifetime.push_back(&elidedOutputLifetime);
            }

            // Visit return type (populates path for `impl Trait` in return position
            fcnPath = &p;
            fcnErasedCount = 0;
            {
                TRACE_FUNCTION_F("RET " << item.returnType);
                visitType(item.returnType);
            }
            fcnPath = nullptr;
            fcnPtr = nullptr;

            if (elidedOutputLifetime != HIRLifetimeRef()) {
                currentLifetime.pop_back();
            }
            assert(currentLifetime.empty());

            if (item.receiver == HIRFunction::Receiver::Custom) {
                ASSERT_BUG(Span(), item.receiverType, "Custom receiver without a receiver type");
                this->visitType(*item.receiverType);
            }
            HIRVisitor::visitFunction(p, item);
        }
    };
}

void TypecheckModuleLevel(HIRCrate& crate) {
    Visitor v{crate};
    v.visitCrate(crate);
}
