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
        nv.visit_node(*this);                \
        nv.visit(*this);                     \
    }                                        \
    void ::HIR::ExprVisitorDef::visit(::HIR::nt& n)
#define DEF_VISIT(nt, n, code) \
    DEF_VISIT_H(nt, n) {       \
        code                   \
    }

const char* ::HIR::ExprNode::type_name() const {
    return typeid(*this).name();
}

void ::HIR::ExprVisitor::visit_node_ptr(::HIR::ExprNodeP& nodePtr) {
    assert(nodePtr);
    nodePtr->visit(*this);
}

void ::HIR::ExprVisitor::visit_node(::HIR::ExprNode& node) {
}

void ::HIR::ExprVisitorDef::visit_node_ptr(::HIR::ExprNodeP& nodePtr) {
    assert(nodePtr);
    TRACE_FUNCTION_F(&*nodePtr << " " << nodePtr->type_name());
    nodePtr->visit(*this);
    visit_type(nodePtr->resType);
}

DEF_VISIT_H(ExprNodeBlock, node) {
    TRACE_FUNCTION_F("_Block");
    for (auto& subnode : node.nodes) {
        visit_node_ptr(subnode);
    }
    if (node.valueNode) {
        visit_node_ptr(node.valueNode);
    }
}

DEF_VISIT_H(ExprNodeConstBlock, node) {
    TRACE_FUNCTION_F("_ConstBlock");
    visit_node_ptr(node.inner);
}

DEF_VISIT_H(ExprNodeAsm, node) {
    TRACE_FUNCTION_F("_Asm");
    for (auto& v : node.outputs) {
        visit_node_ptr(v.value);
    }
    for (auto& v : node.inputs) {
        visit_node_ptr(v.value);
    }
}

DEF_VISIT_H(ExprNodeAsm2, node) {
    TRACE_FUNCTION_F("_Asm2");
    for (auto& v : node.mParams) {
        TU_MATCH_HDRA( (v), { )
        TU_ARMA(Const, e) {
                visit_node_ptr(e);
            }
            TU_ARMA(Sym, e) {
                visit_path(::HIR::Visitor::PathContext::VALUE, e);
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

DEF_VISIT_H(ExprNodeReturn, node) {
    TRACE_FUNCTION_F("_Return");
    visit_node_ptr(node.mValue);
}

DEF_VISIT_H(ExprNodeYield, node) {
    TRACE_FUNCTION_F("_Yield");
    visit_node_ptr(node.mValue);
}

DEF_VISIT_H(ExprNodeAWait, node) {
    TRACE_FUNCTION_F("_AWait");
    visit_node_ptr(node.mValue);
}

DEF_VISIT_H(ExprNodeLet, node) {
    TRACE_FUNCTION_F("_Let: " << node.pattern);
    // Visit the value FIRST as it's evaluated before the variable is defined
    if (node.mValue) {
        visit_node_ptr(node.mValue);
    }
    visit_pattern(node.span(), node.pattern);
    visit_type(node.mType);
}

DEF_VISIT_H(ExprNodeLoop, node) {
    TRACE_FUNCTION_F("_Loop");
    visit_node_ptr(node.mCode);
}

DEF_VISIT_H(ExprNodeLoopControl, node) {
    TRACE_FUNCTION_F("_LoopControl");
    if (node.mValue) {
        visit_node_ptr(node.mValue);
    }
}

DEF_VISIT_H(ExprNodeMatch, node) {
    TRACE_FUNCTION_F("_Match");
    visit_node_ptr(node.mValue);
    for (auto& arm : node.arms) {
        for (auto& pat : arm.patterns) {
            visit_pattern(node.span(), pat);
        }
        for (auto& c : arm.guards) {
            visit_pattern(node.span(), c.pat);
            visit_node_ptr(c.val);
        }
        visit_node_ptr(arm.mCode);
    }
}

DEF_VISIT(ExprNodeAssign, node, TRACE_FUNCTION_F("_Assign"); visit_node_ptr(node.slot); visit_node_ptr(node.mValue);)
DEF_VISIT(ExprNodeBinOp, node, TRACE_FUNCTION_F("_BinOp"); visit_node_ptr(node.left); visit_node_ptr(node.right);)
DEF_VISIT(ExprNodeUniOp, node, TRACE_FUNCTION_F("_UniOp"); visit_node_ptr(node.mValue);)
DEF_VISIT(ExprNodeBorrow, node, TRACE_FUNCTION_F("_Borrow"); visit_node_ptr(node.mValue);)
DEF_VISIT(ExprNodeRawBorrow, node, visit_node_ptr(node.mValue);)

DEF_VISIT_H(ExprNodeCast, node) {
    TRACE_FUNCTION_F("_Cast " << node.dstType);
    visit_type(node.dstType);
    visit_node_ptr(node.mValue);
}

DEF_VISIT_H(ExprNodeUnsize, node) {
    TRACE_FUNCTION_F("_Unsize " << node.dstType);
    visit_type(node.dstType);
    visit_node_ptr(node.mValue);
}

DEF_VISIT_H(ExprNodeIndex, node) {
    TRACE_FUNCTION_F("_Index");
    visit_node_ptr(node.mValue);
    visit_node_ptr(node.index);
}

DEF_VISIT_H(ExprNodeDeref, node) {
    TRACE_FUNCTION_F("_Deref");
    visit_node_ptr(node.mValue);
}

DEF_VISIT_H(ExprNodeEmplace, node) {
    TRACE_FUNCTION_F("_Emplace");
    if (node.place) {
        visit_node_ptr(node.place);
    }
    visit_node_ptr(node.mValue);
}

DEF_VISIT_H(ExprNodeTupleVariant, node) {
    TRACE_FUNCTION_F("_TupleVariant: " << node.mPath);
    visit_generic_path(::HIR::Visitor::PathContext::VALUE, node.mPath);

    for (auto& ty : node.argTypes) {
        if (ty != HIR::TypeRef()) {
            visit_type(ty);
        }
    }

    for (auto& arg : node.mArgs) {
        visit_node_ptr(arg);
    }
}

DEF_VISIT_H(ExprNodeCallPath, node) {
    TRACE_FUNCTION_F("_CallPath: " << node.mPath);
    for (auto& ty : node.cache.argTypes) {
        visit_type(ty);
    }

    visit_path(::HIR::Visitor::PathContext::VALUE, node.mPath);
    for (auto& arg : node.mArgs) {
        visit_node_ptr(arg);
    }
}

DEF_VISIT_H(ExprNodeCallValue, node) {
    TRACE_FUNCTION_F("_CallValue:");
    for (auto& ty : node.argTypes) {
        visit_type(ty);
    }

    visit_node_ptr(node.mValue);
    for (auto& arg : node.mArgs) {
        visit_node_ptr(arg);
    }
}

DEF_VISIT_H(ExprNodeCallMethod, node) {
    TRACE_FUNCTION_FR("_CallMethod: " << node.method, "_CallMethod: " << node.method);
    visit_path_params(node.mParams);
    for (auto& ty : node.cache.argTypes) {
        visit_type(ty);
    }

    visit_path(::HIR::Visitor::PathContext::VALUE, node.methodPath);

    visit_node_ptr(node.mValue);
    for (auto& arg : node.mArgs) {
        visit_node_ptr(arg);
    }
}

DEF_VISIT_H(ExprNodeField, node) {
    TRACE_FUNCTION_F("_Field: " << node.field);
    visit_node_ptr(node.mValue);
}

DEF_VISIT(ExprNodeLiteral, node, TRACE_FUNCTION_F("_Literal");)
DEF_VISIT(ExprNodeUnitVariant, node, TRACE_FUNCTION_F("_UnitVariant: " << node.mPath); visit_generic_path(::HIR::Visitor::PathContext::VALUE, node.mPath);)
DEF_VISIT(ExprNodePathValue, node, TRACE_FUNCTION_F("_PathValue: " << node.mPath); visit_path(::HIR::Visitor::PathContext::VALUE, node.mPath);)
DEF_VISIT(ExprNodeVariable, node, TRACE_FUNCTION_F("_Variable: #" << node.slot);)
DEF_VISIT(ExprNodeConstParam, node, TRACE_FUNCTION_F("_ConstParam");)

DEF_VISIT_H(ExprNodeStructLiteral, node) {
    TRACE_FUNCTION_F("_StructLiteral: " << node.realPath);
    if (node.mType != HIR::TypeRef()) {
        visit_type(node.mType);
    }
    if (node.baseValue) {
        visit_node_ptr(node.baseValue);
    }
    for (auto& val : node.values) {
        visit_node_ptr(val.second);
    }

    visit_generic_path(::HIR::Visitor::PathContext::TYPE, node.realPath);
}

DEF_VISIT_H(ExprNodeTuple, node) {
    TRACE_FUNCTION_F("_Tuple");
    for (auto& val : node.vals) {
        visit_node_ptr(val);
    }
}

DEF_VISIT_H(ExprNodeArrayList, node) {
    TRACE_FUNCTION_F("_ArrayList");
    for (auto& val : node.vals) {
        visit_node_ptr(val);
    }
}
DEF_VISIT(
    ExprNodeArraySized, node, TRACE_FUNCTION_F("_ArraySized"); visit_node_ptr(node.val);
    //visit_arraysize(node.m_size); // Don't do this, array sizes are not part of the normal expression tree
)

DEF_VISIT_H(ExprNodeClosure, node) {
    TRACE_FUNCTION_F("_Closure");
    if (node.objPath != HIR::GenericPath()) {
        for (auto& cap : node.captures) {
            visit_node_ptr(cap);
        }
    } else {
        for (auto& arg : node.mArgs) {
            visit_pattern(node.span(), arg.first);
            visit_type(arg.second);
        }
        visit_type(node.returnType);
        visit_node_ptr(node.mCode);
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
    visit_type(node.returnType);
    visit_type(node.yieldTy);
    visit_type(node.resumeTy);
    if (node.mCode) {
        visit_node_ptr(node.mCode);
    } else {
        for (auto& cap : node.captures) {
            visit_node_ptr(cap);
        }
    }
}

DEF_VISIT_H(ExprNodeGeneratorWrapper, node) {
    //for(auto& arg : node.m_args) {
    //    visit_pattern(node.span(), arg.first);
    //    visit_type(arg.second);
    //}
    visit_type(node.returnType);
    visit_type(node.yieldTy);
    if (node.mCode) {
        visit_node_ptr(node.mCode);
    }
}

DEF_VISIT_H(ExprNodeAsyncBlock, node) {
    TRACE_FUNCTION_F("_AsyncBlock");
    if (node.mCode) {
        visit_node_ptr(node.mCode);
    } else {
    }
}

#undef DEF_VISIT
#undef DEF_VISIT_H

// TODO: Merge this with the stuff in ::HIR::Visitor
void ::HIR::ExprVisitorDef::visit_pattern(const Span& sp, ::HIR::Pattern& pat) {
    TU_MATCH_HDRA( (pat.mData), {)
    TU_ARMA(Any, e) {
        }
        TU_ARMA(Box, e) {
            this->visit_pattern(sp, *e.sub);
        }
        TU_ARMA(Ref, e) {
            this->visit_pattern(sp, *e.sub);
        }
        TU_ARMA(Tuple, e) {
            for (auto& subpat : e.subPatterns) {
                this->visit_pattern(sp, subpat);
            }
        }
        TU_ARMA(SplitTuple, e) {
            for (auto& subpat : e.leading) {
                this->visit_pattern(sp, subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visit_pattern(sp, subpat);
            }
        }
        TU_ARMA(PathValue, e) {
            // Nothing.
            this->visit_path(HIR::Visitor::PathContext::VALUE, e.path);
        }
        TU_ARMA(PathTuple, e) {
            this->visit_path(HIR::Visitor::PathContext::VALUE, e.path);
            for (auto& subpat : e.leading) {
                this->visit_pattern(sp, subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visit_pattern(sp, subpat);
            }
        }
        TU_ARMA(PathNamed, e) {
            this->visit_path(HIR::Visitor::PathContext::TYPE, e.path);
            for (auto& fldPat : e.subPatterns) {
                this->visit_pattern(sp, fldPat.second);
            }
        }
        TU_ARMA(Value, e) {
        }
        TU_ARMA(Range, e) {
        }
        TU_ARMA(Slice, e) {
            for (auto& subpat : e.subPatterns) {
                this->visit_pattern(sp, subpat);
            }
        }
        TU_ARMA(SplitSlice, e) {
            for (auto& subpat : e.leading) {
                this->visit_pattern(sp, subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visit_pattern(sp, subpat);
            }
        }
        TU_ARMA(Or, e) {
            for (auto& subpat : e) {
                this->visit_pattern(sp, subpat);
            }
        }
    }
}

void ::HIR::ExprVisitorDef::visit_type(::HIR::TypeRef& ty) {
    auto data = ty->cloneData();
    TU_MATCH(::HIR::TypeData, (data), (e),
    (Infer,
        ),
    (Diverge,
        ),
    (Primitive,
        ),
    (Path,
        this->visit_path(::HIR::Visitor::PathContext::TYPE, e.path);
        ),
    (Generic,
        ),
    (TraitObject,
        this->visit_trait_path(e.mTrait);
        for(auto& trait : e.markers) {
        this->visit_generic_path(::HIR::Visitor::PathContext::TYPE, trait);
        }
        ),
    (ErasedType,
        for(auto& trait : e.traits) {
        this->visit_trait_path(trait);
        }
        TU_MATCH_HDRA( (e.inner), {)
        TU_ARMA(Known, ee) {
            this->visit_type(ee);
}

TU_ARMA(Fcn, ee) {
    this->visit_path(::HIR::Visitor::PathContext::TYPE, ee.origin);
}

TU_ARMA(Alias, ee) {
}
}
        ),
    (Array,
        this->visit_type( e.inner );
        ),
    (Slice,
        this->visit_type( e.inner );
        ),
    (Tuple,
        for(auto& t : e) {
    this->visit_type(t);
        }
        ),
    (Borrow,
        this->visit_type( e.inner );
        ),
    (Pointer,
        this->visit_type( e.inner );
        ),
    (NamedFunction,
        this->visit_path(::HIR::Visitor::PathContext::VALUE, e.path);
        ),
    (Function,
        for(auto& t : e.argTypes) {
    this->visit_type(t);
        }
        this->visit_type(e.mRettype);
        ),
    (NodeType,
        )
    )
    ty = types.intern(std::move(data));
        }

        void ::HIR::ExprVisitorDef::visit_path_params(::HIR::PathParams& pp) {
            for (auto& ty : pp.types) {
                visit_type(ty);
            }
        }

        void ::HIR::ExprVisitorDef::visit_trait_path(::HIR::TraitPath& p) {
            this->visit_generic_path(::HIR::Visitor::PathContext::TYPE, p.mPath);
            for (auto& assoc : p.typeBounds) {
                this->visit_type(assoc.second.type);
            }
            for (auto& assoc : p.traitBounds) {
                for (auto& t : assoc.second.traits) {
                    this->visit_trait_path(t);
                }
            }
        }

        void ::HIR::ExprVisitorDef::visit_path(::HIR::Visitor::PathContext pc, ::HIR::Path& path) {
            TU_MATCHA((path.mData), (e), (Generic, visit_generic_path(pc, e);), (UfcsKnown, visit_type(e.type); visit_generic_path(pc, e.trait); visit_path_params(e.params);), (UfcsUnknown, visit_type(e.type); visit_path_params(e.params);), (UfcsInherent, visit_type(e.type); visit_path_params(e.params); visit_path_params(e.impl_params);))
        }

        void ::HIR::ExprVisitorDef::visit_generic_path(::HIR::Visitor::PathContext pc, ::HIR::GenericPath& path) {
            visit_path_params(path.mParams);
        }

namespace HIR {

ExprNode::ExprNode(Span sp)
    : mSpan(mv$(sp)) {
}
ExprNodeBlock::ExprNodeBlock(Span sp)
    : ExprNode(mv$(sp))
    , isUnsafe(false) {
}
ExprNodeBlock::ExprNodeBlock(Span sp, bool is_unsafe, ::std::vector<ExprNodeP> nodes, ExprNodeP value_node)
    : ExprNode(mv$(sp))
    , isUnsafe(is_unsafe)
    , nodes(mv$(nodes))
    , valueNode(mv$(value_node)) {
}
ExprNodeConstBlock::ExprNodeConstBlock(Span sp, ExprNodeP inner)
    : ExprNode(mv$(sp))
    , inner(mv$(inner)) {
}
ExprNodeAsm2::ExprNodeAsm2(Span sp, AsmCommon::Options options, std::vector<AsmCommon::Line> lines, std::vector<Param> params)
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
ExprNodeLoop::ExprNodeLoop(Span sp, RcString label, ::HIR::ExprNodeP code, bool require_label)
    : ExprNode(mv$(sp))
    , label(mv$(label))
    , mCode(mv$(code))
    , requireLabel(require_label) {
}
// populated by expr_cs__enum.cpp

ExprNodeLoopControl::ExprNodeLoopControl(Span sp, RcString label, bool cont, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , label(mv$(label))
    , isContinue(cont)
    , mValue(mv$(value))
    , targetNode(nullptr) {
}
ExprNodeLet::ExprNodeLet(Span sp, ::HIR::Pattern pat, ::HIR::TypeRef ty, ::HIR::ExprNodeP val, bool is_super)
    : ExprNode(mv$(sp))
    , pattern(mv$(pat))
    , mType(mv$(ty))
    , mValue(mv$(val))
    , isSuper(is_super) {
}
ExprNodeMatch::ExprNodeMatch(Span sp, ::HIR::ExprNodeP val, ::std::vector<Arm> arms, bool is_let_else)
    : ExprNode(mv$(sp))
    , mValue(mv$(val))
    , arms(mv$(arms))
    , isLetElse(is_let_else) {
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
ExprNodeCast::ExprNodeCast(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dst_type)
    : ExprNode(mv$(sp))
    , mValue(mv$(value))
    , dstType(mv$(dst_type)) {
}
ExprNodeUnsize::ExprNodeUnsize(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dst_type)
    : ExprNode(mv$(sp))
    , mValue(mv$(value))
    , dstType(mv$(dst_type)) {
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
ExprNodeTupleVariant::ExprNodeTupleVariant(Span sp, ::HIR::GenericPath path, bool is_struct, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , mPath(mv$(path))
    , isStruct(is_struct)
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
ExprNodeCallMethod::ExprNodeCallMethod(Span sp, ::HIR::ExprNodeP val, RcString method_name, ::HIR::PathParams params, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , mValue(mv$(val))
    , method(mv$(method_name))
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
ExprNodeUnitVariant::ExprNodeUnitVariant(Span sp, ::HIR::GenericPath path, bool is_struct)
    : ExprNode(mv$(sp))
    , mPath(mv$(path))
    , isStruct(is_struct) {
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
ExprNodeStructLiteral::ExprNodeStructLiteral(Span sp, ::HIR::TypeRef ty, bool is_struct, ::HIR::ExprNodeP base_value, tValues values)
    : ExprNode(mv$(sp))
    , mType(mv$(ty))
    , isStruct(is_struct)
    , baseValue(mv$(base_value))
    , values(mv$(values)) {
}
ExprNodeStructLiteral::ExprNodeStructLiteral(Span sp, ::HIR::TypeRef ty, bool is_struct, bool, tValues values)
    : ExprNode(mv$(sp))
    , mType(mv$(ty))
    , isStruct(is_struct)
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
ExprNodeClosure::ExprNodeClosure(Span sp, argsT args, ::HIR::TypeRef rv, ::HIR::ExprNodeP code, bool is_move)
    : ExprNode(mv$(sp))
    , mArgs(::std::move(args))
    , returnType(::std::move(rv))
    , mCode(::std::move(code))
    , isMove(is_move) {
}
ExprNodeGenerator::ExprNodeGenerator(
    Span sp,
    ::HIR::TypeRef rv,
    ::HIR::TypeRef resume_ty,
    ::HIR::TypeRef yield_ty,
    ::HIR::ExprNodeP code,
    bool is_move,
    bool is_pinned
)
    : ExprNode(mv$(sp))
    , returnType(::std::move(rv))
    , resumeTy(resume_ty)
    , yieldTy(yield_ty)
    , mCode(::std::move(code))
    , isMove(is_move)
    , isPinned(is_pinned) {
}
ExprNodeGeneratorWrapper::ExprNodeGeneratorWrapper(
    Span sp,
    ::HIR::TypeRef rv,
    ::HIR::TypeRef yield_ty,
    ::HIR::ExprNodeP code,
    bool is_future
)
    : ExprNode(mv$(sp))
    , isFuture(is_future)
    , returnType(rv)
    , yieldTy(yield_ty)
    , mCode(::std::move(code)) {
}
ExprNodeAsyncBlock::ExprNodeAsyncBlock(Span sp, ::HIR::ExprNodeP code, bool is_move)
    : ExprNode(mv$(sp))
    , mCode(std::move(code))
    , isMove(is_move) {
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
