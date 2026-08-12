#include "hir_expr.h"

::HIR::ExprNode::~ExprNode() {
}

unsigned int ::HIR::ExprNodeBlock::nodeKind() const { return ::HIR::ExprNodeBlock::kind; }
unsigned int ::HIR::ExprNodeConstBlock::nodeKind() const { return ::HIR::ExprNodeConstBlock::kind; }
unsigned int ::HIR::ExprNodeAsm::nodeKind() const { return ::HIR::ExprNodeAsm::kind; }
unsigned int ::HIR::ExprNodeAsm2::nodeKind() const { return ::HIR::ExprNodeAsm2::kind; }
unsigned int ::HIR::ExprNodeReturn::nodeKind() const { return ::HIR::ExprNodeReturn::kind; }
unsigned int ::HIR::ExprNodeYield::nodeKind() const { return ::HIR::ExprNodeYield::kind; }
unsigned int ::HIR::ExprNodeAWait::nodeKind() const { return ::HIR::ExprNodeAWait::kind; }
unsigned int ::HIR::ExprNodeLoop::nodeKind() const { return ::HIR::ExprNodeLoop::kind; }
unsigned int ::HIR::ExprNodeLoopControl::nodeKind() const { return ::HIR::ExprNodeLoopControl::kind; }
unsigned int ::HIR::ExprNodeLet::nodeKind() const { return ::HIR::ExprNodeLet::kind; }
unsigned int ::HIR::ExprNodeMatch::nodeKind() const { return ::HIR::ExprNodeMatch::kind; }
unsigned int ::HIR::ExprNodeAssign::nodeKind() const { return ::HIR::ExprNodeAssign::kind; }
unsigned int ::HIR::ExprNodeBinOp::nodeKind() const { return ::HIR::ExprNodeBinOp::kind; }
unsigned int ::HIR::ExprNodeUniOp::nodeKind() const { return ::HIR::ExprNodeUniOp::kind; }
unsigned int ::HIR::ExprNodeBorrow::nodeKind() const { return ::HIR::ExprNodeBorrow::kind; }
unsigned int ::HIR::ExprNodeRawBorrow::nodeKind() const { return ::HIR::ExprNodeRawBorrow::kind; }
unsigned int ::HIR::ExprNodeCast::nodeKind() const { return ::HIR::ExprNodeCast::kind; }
unsigned int ::HIR::ExprNodeUnsize::nodeKind() const { return ::HIR::ExprNodeUnsize::kind; }
unsigned int ::HIR::ExprNodeIndex::nodeKind() const { return ::HIR::ExprNodeIndex::kind; }
unsigned int ::HIR::ExprNodeDeref::nodeKind() const { return ::HIR::ExprNodeDeref::kind; }
unsigned int ::HIR::ExprNodeEmplace::nodeKind() const { return ::HIR::ExprNodeEmplace::kind; }
unsigned int ::HIR::ExprNodeTupleVariant::nodeKind() const { return ::HIR::ExprNodeTupleVariant::kind; }
unsigned int ::HIR::ExprNodeCallPath::nodeKind() const { return ::HIR::ExprNodeCallPath::kind; }
unsigned int ::HIR::ExprNodeCallValue::nodeKind() const { return ::HIR::ExprNodeCallValue::kind; }
unsigned int ::HIR::ExprNodeCallMethod::nodeKind() const { return ::HIR::ExprNodeCallMethod::kind; }
unsigned int ::HIR::ExprNodeField::nodeKind() const { return ::HIR::ExprNodeField::kind; }
unsigned int ::HIR::ExprNodeLiteral::nodeKind() const { return ::HIR::ExprNodeLiteral::kind; }
unsigned int ::HIR::ExprNodeUnitVariant::nodeKind() const { return ::HIR::ExprNodeUnitVariant::kind; }
unsigned int ::HIR::ExprNodePathValue::nodeKind() const { return ::HIR::ExprNodePathValue::kind; }
unsigned int ::HIR::ExprNodeVariable::nodeKind() const { return ::HIR::ExprNodeVariable::kind; }
unsigned int ::HIR::ExprNodeConstParam::nodeKind() const { return ::HIR::ExprNodeConstParam::kind; }
unsigned int ::HIR::ExprNodeStructLiteral::nodeKind() const { return ::HIR::ExprNodeStructLiteral::kind; }
unsigned int ::HIR::ExprNodeTuple::nodeKind() const { return ::HIR::ExprNodeTuple::kind; }
unsigned int ::HIR::ExprNodeArrayList::nodeKind() const { return ::HIR::ExprNodeArrayList::kind; }
unsigned int ::HIR::ExprNodeArraySized::nodeKind() const { return ::HIR::ExprNodeArraySized::kind; }
unsigned int ::HIR::ExprNodeClosure::nodeKind() const { return ::HIR::ExprNodeClosure::kind; }
unsigned int ::HIR::ExprNodeGenerator::nodeKind() const { return ::HIR::ExprNodeGenerator::kind; }
unsigned int ::HIR::ExprNodeGeneratorWrapper::nodeKind() const { return ::HIR::ExprNodeGeneratorWrapper::kind; }
unsigned int ::HIR::ExprNodeAsyncBlock::nodeKind() const { return ::HIR::ExprNodeAsyncBlock::kind; }

#define DEF_VISIT_H(nt, n)                   \
    void ::HIR::nt::visit(ExprVisitor& nv) { \
        nv.visitNode(*this);                \
        nv.visit(*this);                     \
    }                                        \
    void ::HIR::ExprVisitorDef::visit(::HIR::nt& n)
#define DEF_VISIT(nt, n, code) \
    DEF_VISIT_H(nt, n) {       \
        code                   \
    }

const char* ::HIR::ExprNode::typeName() const {
    return typeid(*this).name();
}

void ::HIR::ExprVisitor::visitNodePtr(::HIR::ExprNodeP& nodePtr) {
    assert(nodePtr);
    nodePtr->visit(*this);
}

void ::HIR::ExprVisitor::visitNode(::HIR::ExprNode& node) {
}

void ::HIR::ExprVisitorDef::visitNodePtr(::HIR::ExprNodeP& nodePtr) {
    assert(nodePtr);
    TRACE_FUNCTION_F(&*nodePtr << " " << nodePtr->typeName());
    nodePtr->visit(*this);
    visitType(nodePtr->resType);
}

DEF_VISIT_H(ExprNodeBlock, node) {
    TRACE_FUNCTION_F("_Block");
    for (auto& subnode : node.nodes) {
        visitNodePtr(subnode);
    }
    if (node.valueNode) {
        visitNodePtr(node.valueNode);
    }
}

DEF_VISIT_H(ExprNodeConstBlock, node) {
    TRACE_FUNCTION_F("_ConstBlock");
    visitNodePtr(node.inner);
}

DEF_VISIT_H(ExprNodeAsm, node) {
    TRACE_FUNCTION_F("_Asm");
    for (auto& v : node.outputs) {
        visitNodePtr(v.value);
    }
    for (auto& v : node.inputs) {
        visitNodePtr(v.value);
    }
}

DEF_VISIT_H(ExprNodeAsm2, node) {
    TRACE_FUNCTION_F("_Asm2");
    for (auto& v : node.mParams) {
        TU_MATCH_HDRA( (v), { )
        TU_ARMA(Const, e) {
                visitNodePtr(e);
            }
            TU_ARMA(Sym, e) {
                visitPath(::HIR::Visitor::PathContext::VALUE, e);
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

DEF_VISIT_H(ExprNodeReturn, node) {
    TRACE_FUNCTION_F("_Return");
    visitNodePtr(node.mValue);
}

DEF_VISIT_H(ExprNodeYield, node) {
    TRACE_FUNCTION_F("_Yield");
    visitNodePtr(node.mValue);
}

DEF_VISIT_H(ExprNodeAWait, node) {
    TRACE_FUNCTION_F("_AWait");
    visitNodePtr(node.mValue);
}

DEF_VISIT_H(ExprNodeLet, node) {
    TRACE_FUNCTION_F("_Let: " << node.pattern);
    // Visit the value FIRST as it's evaluated before the variable is defined
    if (node.mValue) {
        visitNodePtr(node.mValue);
    }
    visitPattern(node.span(), node.pattern);
    visitType(node.mType);
}

DEF_VISIT_H(ExprNodeLoop, node) {
    TRACE_FUNCTION_F("_Loop");
    visitNodePtr(node.mCode);
}

DEF_VISIT_H(ExprNodeLoopControl, node) {
    TRACE_FUNCTION_F("_LoopControl");
    if (node.mValue) {
        visitNodePtr(node.mValue);
    }
}

DEF_VISIT_H(ExprNodeMatch, node) {
    TRACE_FUNCTION_F("_Match");
    visitNodePtr(node.mValue);
    for (auto& arm : node.arms) {
        for (auto& pat : arm.patterns) {
            visitPattern(node.span(), pat);
        }
        for (auto& c : arm.guards) {
            visitPattern(node.span(), c.pat);
            visitNodePtr(c.val);
        }
        visitNodePtr(arm.mCode);
    }
}

DEF_VISIT(ExprNodeAssign, node, TRACE_FUNCTION_F("_Assign"); visitNodePtr(node.slot); visitNodePtr(node.mValue);)
DEF_VISIT(ExprNodeBinOp, node, TRACE_FUNCTION_F("_BinOp"); visitNodePtr(node.left); visitNodePtr(node.right);)
DEF_VISIT(ExprNodeUniOp, node, TRACE_FUNCTION_F("_UniOp"); visitNodePtr(node.mValue);)
DEF_VISIT(ExprNodeBorrow, node, TRACE_FUNCTION_F("_Borrow"); visitNodePtr(node.mValue);)
DEF_VISIT(ExprNodeRawBorrow, node, visitNodePtr(node.mValue);)

DEF_VISIT_H(ExprNodeCast, node) {
    TRACE_FUNCTION_F("_Cast " << node.dstType);
    visitType(node.dstType);
    visitNodePtr(node.mValue);
}

DEF_VISIT_H(ExprNodeUnsize, node) {
    TRACE_FUNCTION_F("_Unsize " << node.dstType);
    visitType(node.dstType);
    visitNodePtr(node.mValue);
}

DEF_VISIT_H(ExprNodeIndex, node) {
    TRACE_FUNCTION_F("_Index");
    visitNodePtr(node.mValue);
    visitNodePtr(node.index);
}

DEF_VISIT_H(ExprNodeDeref, node) {
    TRACE_FUNCTION_F("_Deref");
    visitNodePtr(node.mValue);
}

DEF_VISIT_H(ExprNodeEmplace, node) {
    TRACE_FUNCTION_F("_Emplace");
    if (node.place) {
        visitNodePtr(node.place);
    }
    visitNodePtr(node.mValue);
}

DEF_VISIT_H(ExprNodeTupleVariant, node) {
    TRACE_FUNCTION_F("_TupleVariant: " << node.mPath);
    visitGenericPath(::HIR::Visitor::PathContext::VALUE, node.mPath);

    for (auto& ty : node.argTypes) {
        if (ty != HIR::TypeRef()) {
            visitType(ty);
        }
    }

    for (auto& arg : node.mArgs) {
        visitNodePtr(arg);
    }
}

DEF_VISIT_H(ExprNodeCallPath, node) {
    TRACE_FUNCTION_F("_CallPath: " << node.mPath);
    for (auto& ty : node.cache.argTypes) {
        visitType(ty);
    }

    visitPath(::HIR::Visitor::PathContext::VALUE, node.mPath);
    for (auto& arg : node.mArgs) {
        visitNodePtr(arg);
    }
}

DEF_VISIT_H(ExprNodeCallValue, node) {
    TRACE_FUNCTION_F("_CallValue:");
    for (auto& ty : node.argTypes) {
        visitType(ty);
    }

    visitNodePtr(node.mValue);
    for (auto& arg : node.mArgs) {
        visitNodePtr(arg);
    }
}

DEF_VISIT_H(ExprNodeCallMethod, node) {
    TRACE_FUNCTION_FR("_CallMethod: " << node.method, "_CallMethod: " << node.method);
    visitPathParams(node.mParams);
    for (auto& ty : node.cache.argTypes) {
        visitType(ty);
    }

    visitPath(::HIR::Visitor::PathContext::VALUE, node.methodPath);

    visitNodePtr(node.mValue);
    for (auto& arg : node.mArgs) {
        visitNodePtr(arg);
    }
}

DEF_VISIT_H(ExprNodeField, node) {
    TRACE_FUNCTION_F("_Field: " << node.field);
    visitNodePtr(node.mValue);
}

DEF_VISIT(ExprNodeLiteral, node, TRACE_FUNCTION_F("_Literal");)
DEF_VISIT(ExprNodeUnitVariant, node, TRACE_FUNCTION_F("_UnitVariant: " << node.mPath); visitGenericPath(::HIR::Visitor::PathContext::VALUE, node.mPath);)
DEF_VISIT(ExprNodePathValue, node, TRACE_FUNCTION_F("_PathValue: " << node.mPath); visitPath(::HIR::Visitor::PathContext::VALUE, node.mPath);)
DEF_VISIT(ExprNodeVariable, node, TRACE_FUNCTION_F("_Variable: #" << node.slot);)
DEF_VISIT(ExprNodeConstParam, node, TRACE_FUNCTION_F("_ConstParam");)

DEF_VISIT_H(ExprNodeStructLiteral, node) {
    TRACE_FUNCTION_F("_StructLiteral: " << node.realPath);
    if (node.mType != HIR::TypeRef()) {
        visitType(node.mType);
    }
    if (node.baseValue) {
        visitNodePtr(node.baseValue);
    }
    for (auto& val : node.values) {
        visitNodePtr(val.second);
    }

    visitGenericPath(::HIR::Visitor::PathContext::TYPE, node.realPath);
}

DEF_VISIT_H(ExprNodeTuple, node) {
    TRACE_FUNCTION_F("_Tuple");
    for (auto& val : node.vals) {
        visitNodePtr(val);
    }
}

DEF_VISIT_H(ExprNodeArrayList, node) {
    TRACE_FUNCTION_F("_ArrayList");
    for (auto& val : node.vals) {
        visitNodePtr(val);
    }
}
DEF_VISIT(
    ExprNodeArraySized, node, TRACE_FUNCTION_F("_ArraySized"); visitNodePtr(node.val);
    //visit_arraysize(node.m_size); // Don't do this, array sizes are not part of the normal expression tree
)

DEF_VISIT_H(ExprNodeClosure, node) {
    TRACE_FUNCTION_F("_Closure");
    if (node.objPath != HIR::GenericPath()) {
        for (auto& cap : node.captures) {
            visitNodePtr(cap);
        }
    } else {
        for (auto& arg : node.mArgs) {
            visitPattern(node.span(), arg.first);
            visitType(arg.second);
        }
        visitType(node.returnType);
        visitNodePtr(node.mCode);
    }
}

namespace HIR {
    ::std::ostream& operator<<(::std::ostream& os, const ExprNodeClosure::AvuCache::Capture& x) {
        os << "#" << x.rootSlot;
        for (const auto& n : x.fields) {
            if (n == RcString()) {
                os << ".*";
            } else {
                os << "." << n;
            }
        }
        os << "[" << x.usage << "]";
        return os;
    }
}

DEF_VISIT_H(ExprNodeGenerator, node) {
    TRACE_FUNCTION_F("_Generator");
    //for(auto& arg : node.m_args) {
    //    visit_pattern(node.span(), arg.first);
    //    visit_type(arg.second);
    //}
    visitType(node.returnType);
    visitType(node.yieldTy);
    visitType(node.resumeTy);
    if (node.mCode) {
        visitNodePtr(node.mCode);
    } else {
        for (auto& cap : node.captures) {
            visitNodePtr(cap);
        }
    }
}

DEF_VISIT_H(ExprNodeGeneratorWrapper, node) {
    //for(auto& arg : node.m_args) {
    //    visit_pattern(node.span(), arg.first);
    //    visit_type(arg.second);
    //}
    visitType(node.returnType);
    visitType(node.yieldTy);
    if (node.mCode) {
        visitNodePtr(node.mCode);
    }
}

DEF_VISIT_H(ExprNodeAsyncBlock, node) {
    TRACE_FUNCTION_F("_AsyncBlock");
    if (node.mCode) {
        visitNodePtr(node.mCode);
    } else {
    }
}

#undef DEF_VISIT
#undef DEF_VISIT_H

// TODO: Merge this with the stuff in ::HIR::Visitor
void ::HIR::ExprVisitorDef::visitPattern(const Span& sp, ::HIR::Pattern& pat) {
    TU_MATCH_HDRA( (pat.mData), {)
    TU_ARMA(Any, e) {
        }
        TU_ARMA(Box, e) {
            this->visitPattern(sp, *e.sub);
        }
        TU_ARMA(Ref, e) {
            this->visitPattern(sp, *e.sub);
        }
        TU_ARMA(Tuple, e) {
            for (auto& subpat : e.subPatterns) {
                this->visitPattern(sp, subpat);
            }
        }
        TU_ARMA(SplitTuple, e) {
            for (auto& subpat : e.leading) {
                this->visitPattern(sp, subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visitPattern(sp, subpat);
            }
        }
        TU_ARMA(PathValue, e) {
            // Nothing.
            this->visitPath(HIR::Visitor::PathContext::VALUE, e.path);
        }
        TU_ARMA(PathTuple, e) {
            this->visitPath(HIR::Visitor::PathContext::VALUE, e.path);
            for (auto& subpat : e.leading) {
                this->visitPattern(sp, subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visitPattern(sp, subpat);
            }
        }
        TU_ARMA(PathNamed, e) {
            this->visitPath(HIR::Visitor::PathContext::TYPE, e.path);
            for (auto& fldPat : e.subPatterns) {
                this->visitPattern(sp, fldPat.second);
            }
        }
        TU_ARMA(Value, e) {
        }
        TU_ARMA(Range, e) {
        }
        TU_ARMA(Slice, e) {
            for (auto& subpat : e.subPatterns) {
                this->visitPattern(sp, subpat);
            }
        }
        TU_ARMA(SplitSlice, e) {
            for (auto& subpat : e.leading) {
                this->visitPattern(sp, subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visitPattern(sp, subpat);
            }
        }
        TU_ARMA(Or, e) {
            for (auto& subpat : e) {
                this->visitPattern(sp, subpat);
            }
        }
    }
}

void ::HIR::ExprVisitorDef::visitType(::HIR::TypeRef& ty) {
    auto data = ty->cloneData();
    TU_MATCH(::HIR::TypeData, (data), (e),
    (Infer,
        ),
    (Diverge,
        ),
    (Primitive,
        ),
    (Path,
        this->visitPath(::HIR::Visitor::PathContext::TYPE, e.path);
        ),
    (Generic,
        ),
    (TraitObject,
        this->visitTraitPath(e.mTrait);
        for(auto& trait : e.markers) {
        this->visitGenericPath(::HIR::Visitor::PathContext::TYPE, trait);
        }
        ),
    (ErasedType,
        for(auto& trait : e.traits) {
        this->visitTraitPath(trait);
        }
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Known, ee) {
            this->visitType(ee);
}

TU_ARMA(Fcn, ee) {
    this->visitPath(::HIR::Visitor::PathContext::TYPE, ee.origin);
}

TU_ARMA(Alias, ee) {
}
}
        ),
    (Array,
        this->visitType( e.inner );
        ),
    (Slice,
        this->visitType( e.inner );
        ),
    (Tuple,
        for(auto& t : e) {
    this->visitType(t);
        }
        ),
    (Borrow,
        this->visitType( e.inner );
        ),
    (Pointer,
        this->visitType( e.inner );
        ),
    (NamedFunction,
        this->visitPath(::HIR::Visitor::PathContext::VALUE, e.path);
        ),
    (Function,
        for(auto& t : e.argTypes) {
    this->visitType(t);
        }
        this->visitType(e.mRettype);
        ),
    (NodeType,
        )
    )
    ty = types.intern(std::move(data));
        }

        void ::HIR::ExprVisitorDef::visitPathParams(::HIR::PathParams& pp) {
            for (auto& ty : pp.types) {
                visitType(ty);
            }
        }

        void ::HIR::ExprVisitorDef::visitTraitPath(::HIR::TraitPath& p) {
            this->visitGenericPath(::HIR::Visitor::PathContext::TYPE, p.mPath);
            for (auto& assoc : p.typeBounds) {
                this->visitType(assoc.second.type);
            }
            for (auto& assoc : p.traitBounds) {
                for (auto& t : assoc.second.traits) {
                    this->visitTraitPath(t);
                }
            }
        }

        void ::HIR::ExprVisitorDef::visitPath(::HIR::Visitor::PathContext pc, ::HIR::Path& path) {
            TU_MATCHA((path.mData), (e), (Generic, visitGenericPath(pc, e);), (UfcsKnown, visitType(e.type); visitGenericPath(pc, e.trait); visitPathParams(e.params);), (UfcsUnknown, visitType(e.type); visitPathParams(e.params);), (UfcsInherent, visitType(e.type); visitPathParams(e.params); visitPathParams(e.implParams);))
        }

        void ::HIR::ExprVisitorDef::visitGenericPath(::HIR::Visitor::PathContext pc, ::HIR::GenericPath& path) {
            visitPathParams(path.mParams);
        }

namespace HIR {

ExprNode::ExprNode(Span sp)
    : mSpan(mv$(sp)) {
}
ExprNodeBlock::ExprNodeBlock(Span sp)
    : ExprNode(mv$(sp))
    , mIsUnsafe(false) {
}
ExprNodeBlock::ExprNodeBlock(Span sp, bool isUnsafe, ::std::vector<ExprNodeP> nodes, ExprNodeP valueNode)
    : ExprNode(mv$(sp))
    , mIsUnsafe(isUnsafe)
    , nodes(mv$(nodes))
    , valueNode(mv$(valueNode)) {
}
ExprNodeConstBlock::ExprNodeConstBlock(Span sp, ExprNodeP inner)
    : ExprNode(mv$(sp))
    , inner(mv$(inner)) {
}
ExprNodeAsm2::ExprNodeAsm2(Span sp, AsmOptions options, std::vector<AsmLine> lines, std::vector<Param> params)
    : ExprNode(mv$(sp))
    , options(options)
    , lines(::std::move(lines))
    , mParams(::std::move(params)) {
}
ExprNodeReturn::ExprNodeReturn(Span sp, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , mValue(mv$(value)) {
}
ExprNodeYield::ExprNodeYield(Span sp, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , mValue(mv$(value)) {
}
ExprNodeAWait::ExprNodeAWait(Span sp, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , mValue(mv$(value)) {
}
ExprNodeLoop::ExprNodeLoop(Span sp, RcString label, ::HIR::ExprNodeP code, bool requireLabel)
    : ExprNode(mv$(sp))
    , label(mv$(label))
    , mCode(mv$(code))
    , requireLabel(requireLabel) {
}
// populated by expr_cs__enum.cpp

ExprNodeLoopControl::ExprNodeLoopControl(Span sp, RcString label, bool cont, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , label(mv$(label))
    , isContinue(cont)
    , mValue(mv$(value))
    , targetNode(nullptr) {
}
ExprNodeLet::ExprNodeLet(Span sp, ::HIR::Pattern pat, ::HIR::TypeRef ty, ::HIR::ExprNodeP val, bool isSuper)
    : ExprNode(mv$(sp))
    , pattern(mv$(pat))
    , mType(mv$(ty))
    , mValue(mv$(val))
    , isSuper(isSuper) {
}
ExprNodeMatch::ExprNodeMatch(Span sp, ::HIR::ExprNodeP val, ::std::vector<Arm> arms, bool isLetElse)
    : ExprNode(mv$(sp))
    , mValue(mv$(val))
    , arms(mv$(arms))
    , isLetElse(isLetElse) {
}
const char* ExprNodeAssign::opname(Op v) {
    switch (v) {
        case Op::None:
            return "";
        case Op::Add:
            return "+";
        case Op::Sub:
            return "-";
        case Op::Mul:
            return "*";
        case Op::Div:
            return "/";
        case Op::Mod:
            return "%";

        case Op::And:
            return "&";
        case Op::Or:
            return "|";
        case Op::Xor:
            return "^";

        case Op::Shr:
            return ">>";
        case Op::Shl:
            return "<<";
    }
    throw "";
}
ExprNodeAssign::ExprNodeAssign(Span sp, Op op, ::HIR::ExprNodeP slot, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , op(op)
    , slot(mv$(slot))
    , mValue(mv$(value)) {
}
const char* ExprNodeBinOp::opname(Op v) {
    switch (v) {
        case Op::CmpEqu:
            return "==";
        case Op::CmpNEqu:
            return "!=";
        case Op::CmpLt:
            return "<";
        case Op::CmpLtE:
            return "<=";
        case Op::CmpGt:
            return ">";
        case Op::CmpGtE:
            return ">=";

        case Op::BoolAnd:
            return "&&";
        case Op::BoolOr:
            return "||";

        case Op::Add:
            return "+";
        case Op::Sub:
            return "-";
        case Op::Mul:
            return "*";
        case Op::Div:
            return "/";
        case Op::Mod:
            return "%";

        case Op::And:
            return "&";
        case Op::Or:
            return "|";
        case Op::Xor:
            return "^";

        case Op::Shr:
            return ">>";
        case Op::Shl:
            return "<<";
    }
    return "??";
}
ExprNodeBinOp::ExprNodeBinOp(Span sp, Op op, ::HIR::ExprNodeP left, ::HIR::ExprNodeP right)
    : ExprNode(mv$(sp))
    , op(op)
    , left(mv$(left))
    , right(mv$(right)) {
}
const char* ExprNodeUniOp::opname(Op v) {
    switch (v) {
        case Op::Invert:
            return "!";
        case Op::Negate:
            return "-";
    }
    throw "";
}
ExprNodeUniOp::ExprNodeUniOp(Span sp, Op op, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , op(op)
    , mValue(mv$(value)) {
}
ExprNodeBorrow::ExprNodeBorrow(Span sp, ::HIR::BorrowType bt, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , mType(bt)
    , mValue(mv$(value))
    , isValidStaticBorrowConstant(false) {
}
ExprNodeRawBorrow::ExprNodeRawBorrow(Span sp, ::HIR::BorrowType bt, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , mType(bt)
    , mValue(mv$(value)) {
}
ExprNodeCast::ExprNodeCast(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dstType)
    : ExprNode(mv$(sp))
    , mValue(mv$(value))
    , dstType(mv$(dstType)) {
}
ExprNodeUnsize::ExprNodeUnsize(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dstType)
    : ExprNode(mv$(sp))
    , mValue(mv$(value))
    , dstType(mv$(dstType)) {
}
ExprNodeIndex::ExprNodeIndex(Span sp, ::HIR::ExprNodeP val, ::HIR::ExprNodeP index)
    : ExprNode(mv$(sp))
    , mValue(mv$(val))
    , index(mv$(index)) {
}
ExprNodeDeref::ExprNodeDeref(Span sp, ::HIR::ExprNodeP val)
    : ExprNode(mv$(sp))
    , mValue(mv$(val))
    , traitUsed(TraitUsed::Unknown) {
}
ExprNodeEmplace::ExprNodeEmplace(Span sp, Type ty, ::HIR::ExprNodeP place, ::HIR::ExprNodeP val)
    : ExprNode(mv$(sp))
    , mType(ty)
    , place(mv$(place))
    , mValue(mv$(val)) {
}
ExprNodeTupleVariant::ExprNodeTupleVariant(Span sp, ::HIR::GenericPath path, bool isStruct, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , mPath(mv$(path))
    , isStruct(isStruct)
    , mArgs(mv$(args)) {
}
ExprNodeCallPath::ExprNodeCallPath(Span sp, ::HIR::Path path, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , mPath(mv$(path))
    , mArgs(mv$(args)) {
}
ExprNodeCallValue::ExprNodeCallValue(Span sp, ::HIR::ExprNodeP val, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , mValue(mv$(val))
    , mArgs(mv$(args)) {
}
ExprNodeCallMethod::ExprNodeCallMethod(Span sp, ::HIR::ExprNodeP val, RcString methodName, ::HIR::PathParams params, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , mValue(mv$(val))
    , method(mv$(methodName))
    , mParams(mv$(params))
    , mArgs(mv$(args))
    ,

    methodPath(::HIR::SimplePath("", {})) {
}
ExprNodeField::ExprNodeField(Span sp, ::HIR::ExprNodeP val, RcString field)
    : ExprNode(mv$(sp))
    , mValue(mv$(val))
    , field(mv$(field)) {
}
ExprNodeLiteral::ExprNodeLiteral(Span sp, Data data)
    : ExprNode(mv$(sp))
    , mData(mv$(data)) {
}
ExprNodeUnitVariant::ExprNodeUnitVariant(Span sp, ::HIR::GenericPath path, bool isStruct)
    : ExprNode(mv$(sp))
    , mPath(mv$(path))
    , isStruct(isStruct) {
}
ExprNodePathValue::ExprNodePathValue(Span sp, ::HIR::Path path, Target target)
    : ExprNode(mv$(sp))
    , mPath(mv$(path))
    , target(target) {
}
ExprNodeVariable::ExprNodeVariable(Span sp, RcString name, unsigned int slot)
    : ExprNode(mv$(sp))
    , mName(mv$(name))
    , slot(slot) {
}
ExprNodeConstParam::ExprNodeConstParam(Span sp, RcString name, unsigned int binding)
    : ExprNode(mv$(sp))
    , mName(mv$(name))
    , mBinding(binding) {
}
ExprNodeStructLiteral::ExprNodeStructLiteral(Span sp, ::HIR::TypeRef ty, bool isStruct, ::HIR::ExprNodeP baseValue, tValues values)
    : ExprNode(mv$(sp))
    , mType(mv$(ty))
    , isStruct(isStruct)
    , baseValue(mv$(baseValue))
    , values(mv$(values)) {
}
ExprNodeStructLiteral::ExprNodeStructLiteral(Span sp, ::HIR::TypeRef ty, bool isStruct, bool, tValues values)
    : ExprNode(mv$(sp))
    , mType(mv$(ty))
    , isStruct(isStruct)
    , useDefaults(true)
    , values(mv$(values)) {
}
ExprNodeTuple::ExprNodeTuple(Span sp, ::std::vector<::HIR::ExprNodeP> vals)
    : ExprNode(mv$(sp))
    , vals(mv$(vals)) {
}
ExprNodeArrayList::ExprNodeArrayList(Span sp, ::std::vector<::HIR::ExprNodeP> vals)
    : ExprNode(mv$(sp))
    , vals(mv$(vals)) {
}
ExprNodeArraySized::ExprNodeArraySized(Span sp, ::HIR::ExprNodeP val, ::HIR::ExprPtr size)
    : ExprNode(mv$(sp))
    , val(mv$(val))
    , mSize(HIR::ConstGeneric(std::make_unique<HIR::ConstGenericUnevaluated>(mv$(size)))) {
}
ExprNodeClosure::ExprNodeClosure(Span sp, argsT args, ::HIR::TypeRef rv, ::HIR::ExprNodeP code, bool isMove)
    : ExprNode(mv$(sp))
    , mArgs(::std::move(args))
    , returnType(::std::move(rv))
    , mCode(::std::move(code))
    , isMove(isMove) {
}
ExprNodeGenerator::ExprNodeGenerator(
    Span sp,
    ::HIR::TypeRef rv,
    ::HIR::TypeRef resumeTy,
    ::HIR::TypeRef yieldTy,
    ::HIR::ExprNodeP code,
    bool isMove,
    bool isPinned
)
    : ExprNode(mv$(sp))
    , returnType(::std::move(rv))
    , resumeTy(resumeTy)
    , yieldTy(yieldTy)
    , mCode(::std::move(code))
    , isMove(isMove)
    , isPinned(isPinned) {
}
ExprNodeGeneratorWrapper::ExprNodeGeneratorWrapper(
    Span sp,
    ::HIR::TypeRef rv,
    ::HIR::TypeRef yieldTy,
    ::HIR::ExprNodeP code,
    bool isFuture
)
    : ExprNode(mv$(sp))
    , isFuture(isFuture)
    , returnType(rv)
    , yieldTy(yieldTy)
    , mCode(::std::move(code)) {
}
ExprNodeAsyncBlock::ExprNodeAsyncBlock(Span sp, ::HIR::ExprNodeP code, bool isMove)
    : ExprNode(mv$(sp))
    , mCode(std::move(code))
    , isMove(isMove) {
}
ExprVisitorDef::ExprVisitorDef(TypeInterner& types): types(types) {}
}

namespace HIR {

::std::ostream& operator<<(::std::ostream& os, const ValueUsage& x) {
    switch (x) {
        case ValueUsage::Unknown:
            os << "Unknown";
            break;
        case ValueUsage::Borrow:
            os << "Borrow";
            break;
        case ValueUsage::Mutate:
            os << "Mutate";
            break;
        case ValueUsage::Move:
            os << "Move";
            break;
    }
    return os;
}
}
