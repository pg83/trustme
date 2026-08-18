#include "hir_expr.h"

HIRExprNode::~HIRExprNode() {
}

unsigned int HIRExprNodeBlock::nodeKind() const {
    return HIRExprNodeBlock::kind;
}

unsigned int HIRExprNodeConstBlock::nodeKind() const {
    return HIRExprNodeConstBlock::kind;
}

unsigned int HIRExprNodeAsm::nodeKind() const {
    return HIRExprNodeAsm::kind;
}

unsigned int HIRExprNodeAsm2::nodeKind() const {
    return HIRExprNodeAsm2::kind;
}

unsigned int HIRExprNodeReturn::nodeKind() const {
    return HIRExprNodeReturn::kind;
}

unsigned int HIRExprNodeYield::nodeKind() const {
    return HIRExprNodeYield::kind;
}

unsigned int HIRExprNodeAWait::nodeKind() const {
    return HIRExprNodeAWait::kind;
}

unsigned int HIRExprNodeUse::nodeKind() const {
    return HIRExprNodeUse::kind;
}

unsigned int HIRExprNodeLoop::nodeKind() const {
    return HIRExprNodeLoop::kind;
}

unsigned int HIRExprNodeLoopControl::nodeKind() const {
    return HIRExprNodeLoopControl::kind;
}

unsigned int HIRExprNodeLet::nodeKind() const {
    return HIRExprNodeLet::kind;
}

unsigned int HIRExprNodeMatch::nodeKind() const {
    return HIRExprNodeMatch::kind;
}

unsigned int HIRExprNodeAssign::nodeKind() const {
    return HIRExprNodeAssign::kind;
}

unsigned int HIRExprNodeBinOp::nodeKind() const {
    return HIRExprNodeBinOp::kind;
}

unsigned int HIRExprNodeUniOp::nodeKind() const {
    return HIRExprNodeUniOp::kind;
}

unsigned int HIRExprNodeBorrow::nodeKind() const {
    return HIRExprNodeBorrow::kind;
}

unsigned int HIRExprNodeRawBorrow::nodeKind() const {
    return HIRExprNodeRawBorrow::kind;
}

unsigned int HIRExprNodeCast::nodeKind() const {
    return HIRExprNodeCast::kind;
}

unsigned int HIRExprNodeUnsize::nodeKind() const {
    return HIRExprNodeUnsize::kind;
}

unsigned int HIRExprNodeIndex::nodeKind() const {
    return HIRExprNodeIndex::kind;
}

unsigned int HIRExprNodeDeref::nodeKind() const {
    return HIRExprNodeDeref::kind;
}

unsigned int HIRExprNodeEmplace::nodeKind() const {
    return HIRExprNodeEmplace::kind;
}

unsigned int HIRExprNodeTupleVariant::nodeKind() const {
    return HIRExprNodeTupleVariant::kind;
}

unsigned int HIRExprNodeCallPath::nodeKind() const {
    return HIRExprNodeCallPath::kind;
}

unsigned int HIRExprNodeCallValue::nodeKind() const {
    return HIRExprNodeCallValue::kind;
}

unsigned int HIRExprNodeCallMethod::nodeKind() const {
    return HIRExprNodeCallMethod::kind;
}

unsigned int HIRExprNodeField::nodeKind() const {
    return HIRExprNodeField::kind;
}

unsigned int HIRExprNodeLiteral::nodeKind() const {
    return HIRExprNodeLiteral::kind;
}

unsigned int HIRExprNodeUnitVariant::nodeKind() const {
    return HIRExprNodeUnitVariant::kind;
}

unsigned int HIRExprNodePathValue::nodeKind() const {
    return HIRExprNodePathValue::kind;
}

unsigned int HIRExprNodeVariable::nodeKind() const {
    return HIRExprNodeVariable::kind;
}

unsigned int HIRExprNodeConstParam::nodeKind() const {
    return HIRExprNodeConstParam::kind;
}

unsigned int HIRExprNodeStructLiteral::nodeKind() const {
    return HIRExprNodeStructLiteral::kind;
}

unsigned int HIRExprNodeTuple::nodeKind() const {
    return HIRExprNodeTuple::kind;
}

unsigned int HIRExprNodeArrayList::nodeKind() const {
    return HIRExprNodeArrayList::kind;
}

unsigned int HIRExprNodeArraySized::nodeKind() const {
    return HIRExprNodeArraySized::kind;
}

unsigned int HIRExprNodeClosure::nodeKind() const {
    return HIRExprNodeClosure::kind;
}

unsigned int HIRExprNodeGenerator::nodeKind() const {
    return HIRExprNodeGenerator::kind;
}

unsigned int HIRExprNodeGeneratorWrapper::nodeKind() const {
    return HIRExprNodeGeneratorWrapper::kind;
}

unsigned int HIRExprNodeAsyncBlock::nodeKind() const {
    return HIRExprNodeAsyncBlock::kind;
}

#define DEF_VISIT_H(nt, n)               \
    void nt::visit(HIRExprVisitor& nv) { \
        nv.visitNode(*this);             \
        nv.visit(*this);                 \
    }                                    \
    void HIRExprVisitorDef::visit(nt& n)
#define DEF_VISIT(nt, n, code) \
    DEF_VISIT_H(nt, n) {       \
        code                   \
    }

const char* HIRExprNode::typeName() const {
    return typeid(*this).name();
}

void HIRExprVisitor::visitNodePtr(HIRExprNodeP& nodePtr) {
    assert(nodePtr);
    nodePtr->visit(*this);
}

void HIRExprVisitor::visitNode(HIRExprNode& node) {
}

void HIRExprVisitorDef::visitNodePtr(HIRExprNodeP& nodePtr) {
    assert(nodePtr);
    TRACE_FUNCTION_F(&*nodePtr << " " << nodePtr->typeName());
    nodePtr->visit(*this);
    // A node a desugaring built has no result type until type checking gives
    // it one, and a pass that runs before then has nothing to visit.
    if (nodePtr->resType != HIRTypeRef()) {
        visitType(nodePtr->resType);
    }
}

DEF_VISIT_H(HIRExprNodeBlock, node) {
    TRACE_FUNCTION_F("_Block");
    for (auto& subnode : node.nodes) {
        visitNodePtr(subnode);
    }
    if (node.valueNode) {
        visitNodePtr(node.valueNode);
    }
}

DEF_VISIT_H(HIRExprNodeConstBlock, node) {
    TRACE_FUNCTION_F("_ConstBlock");
    visitNodePtr(node.inner);
}

DEF_VISIT_H(HIRExprNodeAsm, node) {
    TRACE_FUNCTION_F("_Asm");
    for (auto& v : node.outputs) {
        visitNodePtr(v.value);
    }
    for (auto& v : node.inputs) {
        visitNodePtr(v.value);
    }
}

DEF_VISIT_H(HIRExprNodeAsm2, node) {
    TRACE_FUNCTION_F("_Asm2");
    for (auto& v : node.params) {
        TU_MATCH_HDRA( (v), { )
        TU_ARMA(Const, e) {
                visitNodePtr(e);
            }
            TU_ARMA(Sym, e) {
                visitPath(HIRVisitor::PathContext::VALUE, e);
            }
            TU_ARMA(Label, e) {
                visitNodePtr(e.code);
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

DEF_VISIT_H(HIRExprNodeReturn, node) {
    TRACE_FUNCTION_F("_Return");
    visitNodePtr(node.value);
}

DEF_VISIT_H(HIRExprNodeYield, node) {
    TRACE_FUNCTION_F("_Yield");
    visitNodePtr(node.value);
}

DEF_VISIT_H(HIRExprNodeAWait, node) {
    TRACE_FUNCTION_F("_AWait");
    visitNodePtr(node.value);
}

DEF_VISIT_H(HIRExprNodeUse, node) {
    TRACE_FUNCTION_F("_Use");
    visitNodePtr(node.value);
}

DEF_VISIT_H(HIRExprNodeLet, node) {
    TRACE_FUNCTION_F("_Let: " << node.pattern);
    // Visit the value FIRST as it's evaluated before the variable is defined
    if (node.value) {
        visitNodePtr(node.value);
    }
    visitPattern(node.span(), node.pattern);
    visitType(node.type);
}

DEF_VISIT_H(HIRExprNodeLoop, node) {
    TRACE_FUNCTION_F("_Loop");
    visitNodePtr(node.code);
}

DEF_VISIT_H(HIRExprNodeLoopControl, node) {
    TRACE_FUNCTION_F("_LoopControl");
    if (node.value) {
        visitNodePtr(node.value);
    }
}

DEF_VISIT_H(HIRExprNodeMatch, node) {
    TRACE_FUNCTION_F("_Match");
    visitNodePtr(node.value);
    for (auto& arm : node.arms) {
        for (auto& pat : arm.patterns) {
            visitPattern(node.span(), pat);
        }
        for (auto& c : arm.guards) {
            visitPattern(node.span(), c.pat);
            visitNodePtr(c.val);
        }
        visitNodePtr(arm.code);
    }
}

DEF_VISIT(HIRExprNodeAssign, node, TRACE_FUNCTION_F("_Assign"); visitNodePtr(node.slot); visitNodePtr(node.value);)
DEF_VISIT(HIRExprNodeBinOp, node, TRACE_FUNCTION_F("_BinOp"); visitNodePtr(node.left); visitNodePtr(node.right);)
DEF_VISIT(HIRExprNodeUniOp, node, TRACE_FUNCTION_F("_UniOp"); visitNodePtr(node.value);)
DEF_VISIT(HIRExprNodeBorrow, node, TRACE_FUNCTION_F("_Borrow"); visitNodePtr(node.value);)
DEF_VISIT(HIRExprNodeRawBorrow, node, visitNodePtr(node.value);)

DEF_VISIT_H(HIRExprNodeCast, node) {
    TRACE_FUNCTION_F("_Cast " << node.dstType);
    visitType(node.dstType);
    visitNodePtr(node.value);
}

DEF_VISIT_H(HIRExprNodeUnsize, node) {
    TRACE_FUNCTION_F("_Unsize " << node.dstType);
    visitType(node.dstType);
    visitNodePtr(node.value);
}

DEF_VISIT_H(HIRExprNodeIndex, node) {
    TRACE_FUNCTION_F("_Index");
    visitNodePtr(node.value);
    visitNodePtr(node.index);
}

DEF_VISIT_H(HIRExprNodeDeref, node) {
    TRACE_FUNCTION_F("_Deref");
    visitNodePtr(node.value);
}

DEF_VISIT_H(HIRExprNodeEmplace, node) {
    TRACE_FUNCTION_F("_Emplace");
    if (node.place) {
        visitNodePtr(node.place);
    }
    visitNodePtr(node.value);
}

DEF_VISIT_H(HIRExprNodeTupleVariant, node) {
    TRACE_FUNCTION_F("_TupleVariant: " << node.path);
    visitGenericPath(HIRVisitor::PathContext::VALUE, node.path);

    for (auto& ty : node.argTypes) {
        if (ty != HIRTypeRef()) {
            visitType(ty);
        }
    }

    for (auto& arg : node.args) {
        visitNodePtr(arg);
    }
}

DEF_VISIT_H(HIRExprNodeCallPath, node) {
    TRACE_FUNCTION_F("_CallPath: " << node.path);
    for (auto& ty : node.cache.argTypes) {
        visitType(ty);
    }

    visitPath(HIRVisitor::PathContext::VALUE, node.path);
    for (auto& arg : node.args) {
        visitNodePtr(arg);
    }
}

DEF_VISIT_H(HIRExprNodeCallValue, node) {
    TRACE_FUNCTION_F("_CallValue:");
    for (auto& ty : node.argTypes) {
        visitType(ty);
    }

    visitNodePtr(node.value);
    for (auto& arg : node.args) {
        visitNodePtr(arg);
    }
}

DEF_VISIT_H(HIRExprNodeCallMethod, node) {
    TRACE_FUNCTION_FR("_CallMethod: " << node.method, "_CallMethod: " << node.method);
    visitPathParams(node.params);
    for (auto& ty : node.cache.argTypes) {
        visitType(ty);
    }

    visitPath(HIRVisitor::PathContext::VALUE, node.methodPath);

    visitNodePtr(node.value);
    for (auto& arg : node.args) {
        visitNodePtr(arg);
    }
}

DEF_VISIT_H(HIRExprNodeField, node) {
    TRACE_FUNCTION_F("_Field: " << node.field);
    visitNodePtr(node.value);
}

DEF_VISIT(HIRExprNodeLiteral, node, TRACE_FUNCTION_F("_Literal");)
DEF_VISIT(HIRExprNodeUnitVariant, node, TRACE_FUNCTION_F("_UnitVariant: " << node.path); visitGenericPath(HIRVisitor::PathContext::VALUE, node.path);)
DEF_VISIT(HIRExprNodePathValue, node, TRACE_FUNCTION_F("_PathValue: " << node.path); visitPath(HIRVisitor::PathContext::VALUE, node.path);)
DEF_VISIT(HIRExprNodeVariable, node, TRACE_FUNCTION_F("_Variable: #" << node.slot);)
DEF_VISIT(HIRExprNodeConstParam, node, TRACE_FUNCTION_F("_ConstParam");)

DEF_VISIT_H(HIRExprNodeStructLiteral, node) {
    TRACE_FUNCTION_F("_StructLiteral: " << node.realPath);
    if (node.type != HIRTypeRef()) {
        visitType(node.type);
    }
    if (node.baseValue) {
        visitNodePtr(node.baseValue);
    }
    for (auto& val : node.values) {
        visitNodePtr(val.second);
    }

    visitGenericPath(HIRVisitor::PathContext::TYPE, node.realPath);
}

DEF_VISIT_H(HIRExprNodeTuple, node) {
    TRACE_FUNCTION_F("_Tuple");
    for (auto& val : node.vals) {
        visitNodePtr(val);
    }
}

DEF_VISIT_H(HIRExprNodeArrayList, node) {
    TRACE_FUNCTION_F("_ArrayList");
    for (auto& val : node.vals) {
        visitNodePtr(val);
    }
}
DEF_VISIT(
    HIRExprNodeArraySized, node, TRACE_FUNCTION_F("_ArraySized"); visitNodePtr(node.val);
    //visit_arraysize(node.m_size); // Don't do this, array sizes are not part of the normal expression tree
)

DEF_VISIT_H(HIRExprNodeClosure, node) {
    TRACE_FUNCTION_F("_Closure");
    if (node.objPath != HIRGenericPath()) {
        for (auto& cap : node.captures) {
            visitNodePtr(cap);
        }
    } else {
        for (auto& arg : node.args) {
            visitPattern(node.span(), arg.first);
            visitType(arg.second);
        }
        visitType(node.returnType);
        visitNodePtr(node.code);
    }
}

::std::ostream& operator<<(::std::ostream& os, const HIRExprNodeClosure::AvuCache::Capture& x) {
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

DEF_VISIT_H(HIRExprNodeGenerator, node) {
    TRACE_FUNCTION_F("_Generator");
    visitType(node.returnType);
    visitType(node.yieldTy);
    visitType(node.resumeTy);
    if (node.hasResumePattern) {
        visitPattern(node.span(), node.resumePattern);
    }
    if (node.code) {
        visitNodePtr(node.code);
    } else {
        for (auto& cap : node.captures) {
            visitNodePtr(cap);
        }
    }
}

DEF_VISIT_H(HIRExprNodeGeneratorWrapper, node) {
    //}
    visitType(node.returnType);
    visitType(node.yieldTy);
    if (node.code) {
        visitNodePtr(node.code);
    }
}

DEF_VISIT_H(HIRExprNodeAsyncBlock, node) {
    TRACE_FUNCTION_F("_AsyncBlock");
    visitType(node.returnType);
    if (node.code) {
        visitNodePtr(node.code);
    } else {
    }
}

#undef DEF_VISIT
#undef DEF_VISIT_H

// TODO: Merge this with the stuff in ::HIR::Visitor
void HIRExprVisitorDef::visitPattern(const Span& sp, HIRPattern& pat) {
    TU_MATCH_HDRA( (pat.data), {)
    TU_ARMA(Any, e) {
        }
        TU_ARMA(Box, e) {
            this->visitPattern(sp, *e.sub);
        }
        TU_ARMA(Deref, e) {
            if (e.targetType) this->visitType(e.targetType);
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
            this->visitPath(HIRVisitor::PathContext::VALUE, e.path);
        }
        TU_ARMA(PathTuple, e) {
            this->visitPath(HIRVisitor::PathContext::VALUE, e.path);
            for (auto& subpat : e.leading) {
                this->visitPattern(sp, subpat);
            }
            for (auto& subpat : e.trailing) {
                this->visitPattern(sp, subpat);
            }
        }
        TU_ARMA(PathNamed, e) {
            this->visitPath(HIRVisitor::PathContext::TYPE, e.path);
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

void HIRExprVisitorDef::visitType(HIRTypeRef& ty) {
    auto data = ty->cloneData();
    switch (data.tag()) {
        case HIRTypeData::TAG_Infer: {
            auto& e = data.as_Infer();
            (void)e;
            break;
        }
        case HIRTypeData::TAG_Diverge: {
            auto& e = data.as_Diverge();
            (void)e;
            break;
        }
        case HIRTypeData::TAG_Primitive: {
            auto& e = data.as_Primitive();
            (void)e;
            break;
        }
        case HIRTypeData::TAG_Path: {
            auto& e = data.as_Path();
            this->visitPath(HIRVisitor::PathContext::TYPE, e.path);
            break;
        }
        case HIRTypeData::TAG_Generic: {
            auto& e = data.as_Generic();
            (void)e;
            break;
        }
        case HIRTypeData::TAG_TraitObject: {
            auto& e = data.as_TraitObject();
            this->visitTraitPath(e.trait);
            for(auto& trait : e.markers) {
            this->visitGenericPath(HIRVisitor::PathContext::TYPE, trait);
            }
            break;
        }
        case HIRTypeData::TAG_ErasedType: {
            auto& e = data.as_ErasedType();
            for(auto& trait : e.traits) {
                    this->visitTraitPath(trait);
                    }
                    TU_MATCH_HDRA( (e.inner), {)
                    TU_ARMA(Known, ee) {
                        this->visitType(ee);
            }

            TU_ARMA(Fcn, ee) {
                this->visitPath(HIRVisitor::PathContext::TYPE, ee.origin);
            }

            TU_ARMA(Alias, ee) {
            }
            }
            break;
        }
        case HIRTypeData::TAG_Array: {
            auto& e = data.as_Array();
            this->visitType( e.inner );
            break;
        }
        case HIRTypeData::TAG_Slice: {
            auto& e = data.as_Slice();
            this->visitType( e.inner );
            break;
        }
        case HIRTypeData::TAG_Pattern: {
            auto& e = data.as_Pattern();
            this->visitType(e.inner);
            break;
        }
        case HIRTypeData::TAG_Tuple: {
            auto& e = data.as_Tuple();
            for(auto& t : e) {
            this->visitType(t);
                }
            break;
        }
        case HIRTypeData::TAG_Borrow: {
            auto& e = data.as_Borrow();
            this->visitType( e.inner );
            break;
        }
        case HIRTypeData::TAG_Pointer: {
            auto& e = data.as_Pointer();
            this->visitType( e.inner );
            break;
        }
        case HIRTypeData::TAG_NamedFunction: {
            auto& e = data.as_NamedFunction();
            this->visitPath(HIRVisitor::PathContext::VALUE, e.path);
            break;
        }
        case HIRTypeData::TAG_Function: {
            auto& e = data.as_Function();
            for(auto& t : e.argTypes) {
            this->visitType(t);
                }
                this->visitType(e.rettype);
            break;
        }
        case HIRTypeData::TAG_NodeType: {
            auto& e = data.as_NodeType();
            (void)e;
            break;
        }
    }
    ty = types.intern(std::move(data));
        }

        void HIRExprVisitorDef::visitPathParams(HIRPathParams& pp) {
            for (auto& ty : pp.types) {
                visitType(ty);
            }
        }

        void HIRExprVisitorDef::visitTraitPath(HIRTraitPath& p) {
            this->visitGenericPath(HIRVisitor::PathContext::TYPE, p.path);
            for (auto& assoc : p.typeBounds) {
                this->visitType(assoc.second.type);
            }
            for (auto& assoc : p.traitBounds) {
                for (auto& t : assoc.second.traits) {
                    this->visitTraitPath(t);
                }
            }
        }

        void HIRExprVisitorDef::visitPath(HIRVisitor::PathContext pc, HIRPath& path) {
            switch (path.data.tag()) {
                case HIRPathData::TAG_Generic: {
                    auto& e = path.data.as_Generic();
                    visitGenericPath(pc, e);
                    break;
                }
                case HIRPathData::TAG_UfcsKnown: {
                    auto& e = path.data.as_UfcsKnown();
                    visitType(e.type); visitGenericPath(pc, e.trait); visitPathParams(e.params);
                    break;
                }
                case HIRPathData::TAG_UfcsUnknown: {
                    auto& e = path.data.as_UfcsUnknown();
                    visitType(e.type); visitPathParams(e.params);
                    break;
                }
                case HIRPathData::TAG_UfcsInherent: {
                    auto& e = path.data.as_UfcsInherent();
                    visitType(e.type); visitPathParams(e.params); visitPathParams(e.implParams);
                    break;
                }
            }
        }

        void HIRExprVisitorDef::visitGenericPath(HIRVisitor::PathContext pc, HIRGenericPath& path) {
            visitPathParams(path.params);
        }

        HIRExprNode::HIRExprNode(Span sp)
            : span_(mv$(sp))
        {
        }

        HIRExprNodeBlock::HIRExprNodeBlock(Span sp)
            : HIRExprNode(mv$(sp))
            , isUnsafe(false)
        {
        }

        HIRExprNodeBlock::HIRExprNodeBlock(Span sp, bool isUnsafe, ::std::vector<HIRExprNodeP> nodes, HIRExprNodeP valueNode)
            : HIRExprNode(mv$(sp))
            , isUnsafe(isUnsafe)
            , nodes(mv$(nodes))
            , valueNode(mv$(valueNode))
        {
        }

        HIRExprNodeConstBlock::HIRExprNodeConstBlock(Span sp, HIRExprNodeP inner)
            : HIRExprNode(mv$(sp))
            , inner(mv$(inner))
        {
        }

        HIRExprNodeAsm2::HIRExprNodeAsm2(Span sp, AsmOptions options, std::vector<AsmLine> lines, std::vector<Param> params)
            : HIRExprNode(mv$(sp))
            , options(options)
            , lines(::std::move(lines))
            , params(::std::move(params))
        {
        }

        HIRExprNodeReturn::HIRExprNodeReturn(Span sp, HIRExprNodeP value, bool isTailCall)
            : HIRExprNode(mv$(sp))
            , value(mv$(value))
            , isTailCall(isTailCall)
        {
        }

        HIRExprNodeYield::HIRExprNodeYield(Span sp, HIRExprNodeP value)
            : HIRExprNode(mv$(sp))
            , value(mv$(value))
        {
        }

        HIRExprNodeAWait::HIRExprNodeAWait(Span sp, HIRExprNodeP value)
            : HIRExprNode(mv$(sp))
            , value(mv$(value))
        {
        }

        HIRExprNodeUse::HIRExprNodeUse(Span sp, HIRExprNodeP value)
            : HIRExprNode(mv$(sp))
            , value(mv$(value))
        {
        }

        HIRExprNodeLoop::HIRExprNodeLoop(Span sp, RcString label, HIRExprNodeP code, bool requireLabel)
            : HIRExprNode(mv$(sp))
            , label(mv$(label))
            , code(mv$(code))
            , requireLabel(requireLabel)
        {
        }

        // populated by expr_cs__enum.cpp

        HIRExprNodeLoopControl::HIRExprNodeLoopControl(Span sp, RcString label, bool cont, HIRExprNodeP value)
            : HIRExprNode(mv$(sp))
            , label(mv$(label))
            , isContinue(cont)
            , value(mv$(value))
            , targetNode(nullptr)
        {
        }

        HIRExprNodeLet::HIRExprNodeLet(Span sp, HIRPattern pat, HIRTypeRef ty, HIRExprNodeP val, bool isSuper)
            : HIRExprNode(mv$(sp))
            , pattern(mv$(pat))
            , type(mv$(ty))
            , value(mv$(val))
            , isSuper(isSuper)
        {
        }

        HIRExprNodeMatch::HIRExprNodeMatch(Span sp, HIRExprNodeP val, ::std::vector<Arm> arms, bool isLetElse)
            : HIRExprNode(mv$(sp))
            , value(mv$(val))
            , arms(mv$(arms))
            , isLetElse(isLetElse)
        {
        }

        const char* HIRExprNodeAssign::opname(Op v) {
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

        HIRExprNodeAssign::HIRExprNodeAssign(Span sp, Op op, HIRExprNodeP slot, HIRExprNodeP value)
            : HIRExprNode(mv$(sp))
            , op(op)
            , slot(mv$(slot))
            , value(mv$(value))
        {
        }

        const char* HIRExprNodeBinOp::opname(Op v) {
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

        HIRExprNodeBinOp::HIRExprNodeBinOp(Span sp, Op op, HIRExprNodeP left, HIRExprNodeP right)
            : HIRExprNode(mv$(sp))
            , op(op)
            , left(mv$(left))
            , right(mv$(right))
        {
        }

        const char* HIRExprNodeUniOp::opname(Op v) {
            switch (v) {
                case Op::Invert:
                    return "!";
                case Op::Negate:
                    return "-";
            }
            throw "";
        }

        HIRExprNodeUniOp::HIRExprNodeUniOp(Span sp, Op op, HIRExprNodeP value)
            : HIRExprNode(mv$(sp))
            , op(op)
            , value(mv$(value))
        {
        }

        HIRExprNodeBorrow::HIRExprNodeBorrow(Span sp, HIRBorrowType bt, HIRExprNodeP value)
            : HIRExprNode(mv$(sp))
            , type(bt)
            , value(mv$(value))
            , isValidStaticBorrowConstant(false)
        {
        }

        HIRExprNodeRawBorrow::HIRExprNodeRawBorrow(Span sp, HIRBorrowType bt, HIRExprNodeP value)
            : HIRExprNode(mv$(sp))
            , type(bt)
            , value(mv$(value))
        {
        }

        HIRExprNodeCast::HIRExprNodeCast(Span sp, HIRExprNodeP value, HIRTypeRef dstType)
            : HIRExprNode(mv$(sp))
            , value(mv$(value))
            , dstType(mv$(dstType))
        {
        }

        HIRExprNodeUnsize::HIRExprNodeUnsize(Span sp, HIRExprNodeP value, HIRTypeRef dstType)
            : HIRExprNode(mv$(sp))
            , value(mv$(value))
            , dstType(mv$(dstType))
        {
        }

        HIRExprNodeIndex::HIRExprNodeIndex(Span sp, HIRExprNodeP val, HIRExprNodeP index)
            : HIRExprNode(mv$(sp))
            , value(mv$(val))
            , index(mv$(index))
        {
        }

        HIRExprNodeDeref::HIRExprNodeDeref(Span sp, HIRExprNodeP val)
            : HIRExprNode(mv$(sp))
            , value(mv$(val))
            , traitUsed(TraitUsed::Unknown)
        {
        }

        HIRExprNodeEmplace::HIRExprNodeEmplace(Span sp, Type ty, HIRExprNodeP place, HIRExprNodeP val)
            : HIRExprNode(mv$(sp))
            , type(ty)
            , place(mv$(place))
            , value(mv$(val))
        {
        }

        HIRExprNodeTupleVariant::HIRExprNodeTupleVariant(Span sp, HIRGenericPath path, bool isStruct, ::std::vector<HIRExprNodeP> args)
            : HIRExprNode(mv$(sp))
            , path(mv$(path))
            , isStruct(isStruct)
            , args(mv$(args))
        {
        }

        HIRExprNodeCallPath::HIRExprNodeCallPath(Span sp, HIRPath path, ::std::vector<HIRExprNodeP> args)
            : HIRExprNode(mv$(sp))
            , path(mv$(path))
            , args(mv$(args))
        {
        }

        HIRExprNodeCallValue::HIRExprNodeCallValue(Span sp, HIRExprNodeP val, ::std::vector<HIRExprNodeP> args)
            : HIRExprNode(mv$(sp))
            , value(mv$(val))
            , args(mv$(args))
        {
        }

        HIRExprNodeCallMethod::HIRExprNodeCallMethod(Span sp, HIRExprNodeP val, RcString methodName, HIRPathParams params, ::std::vector<HIRExprNodeP> args)
            : HIRExprNode(mv$(sp))
            , value(mv$(val))
            , method(mv$(methodName))
            , params(mv$(params))
            , args(mv$(args))
            ,

            methodPath(HIRSimplePath("", {}))
        {
        }

        HIRExprNodeField::HIRExprNodeField(Span sp, HIRExprNodeP val, RcString field)
            : HIRExprNode(mv$(sp))
            , value(mv$(val))
            , field(mv$(field))
        {
        }

        HIRExprNodeLiteral::HIRExprNodeLiteral(Span sp, Data data)
            : HIRExprNode(mv$(sp))
            , data(mv$(data))
        {
        }

        HIRExprNodeUnitVariant::HIRExprNodeUnitVariant(Span sp, HIRGenericPath path, bool isStruct)
            : HIRExprNode(mv$(sp))
            , path(mv$(path))
            , isStruct(isStruct)
        {
        }

        HIRExprNodePathValue::HIRExprNodePathValue(Span sp, HIRPath path, Target target)
            : HIRExprNode(mv$(sp))
            , path(mv$(path))
            , target(target)
        {
        }

        HIRExprNodeVariable::HIRExprNodeVariable(Span sp, RcString name, unsigned int slot)
            : HIRExprNode(mv$(sp))
            , name(mv$(name))
            , slot(slot)
        {
        }

        HIRExprNodeConstParam::HIRExprNodeConstParam(Span sp, RcString name, unsigned int binding)
            : HIRExprNode(mv$(sp))
            , name(mv$(name))
            , binding(binding)
        {
        }

        HIRExprNodeStructLiteral::HIRExprNodeStructLiteral(Span sp, HIRTypeRef ty, bool isStruct, HIRExprNodeP baseValue, tValues values)
            : HIRExprNode(mv$(sp))
            , type(mv$(ty))
            , isStruct(isStruct)
            , baseValue(mv$(baseValue))
            , values(mv$(values))
        {
        }

        HIRExprNodeStructLiteral::HIRExprNodeStructLiteral(Span sp, HIRTypeRef ty, bool isStruct, bool, tValues values)
            : HIRExprNode(mv$(sp))
            , type(mv$(ty))
            , isStruct(isStruct)
            , useDefaults(true)
            , values(mv$(values))
        {
        }

        HIRExprNodeTuple::HIRExprNodeTuple(Span sp, ::std::vector<HIRExprNodeP> vals)
            : HIRExprNode(mv$(sp))
            , vals(mv$(vals))
        {
        }

        HIRExprNodeArrayList::HIRExprNodeArrayList(Span sp, ::std::vector<HIRExprNodeP> vals)
            : HIRExprNode(mv$(sp))
            , vals(mv$(vals))
        {
        }

        HIRExprNodeArraySized::HIRExprNodeArraySized(Span sp, HIRExprNodeP val, HIRExprPtr size)
            : HIRExprNode(mv$(sp))
            , val(mv$(val))
            , size(HIRConstGeneric(std::make_unique<HIRConstGenericUnevaluated>(mv$(size))))
        {
        }

        HIRExprNodeArraySized::HIRExprNodeArraySized(Span sp, HIRExprNodeP val, HIRArraySize size)
            : HIRExprNode(mv$(sp))
            , val(mv$(val))
            , size(mv$(size))
        {
        }

        HIRExprNodeClosure::HIRExprNodeClosure(Span sp, argsT args, HIRTypeRef rv, HIRExprNodeP code, bool isMove, bool isUse)
            : HIRExprNode(mv$(sp))
            , args(::std::move(args))
            , returnType(::std::move(rv))
            , code(::std::move(code))
            , isMove(isMove)
            , isUse(isUse)
        {
        }

        HIRExprNodeGenerator::HIRExprNodeGenerator(Span sp, HIRTypeRef rv, HIRTypeRef resumeTy, HIRPattern resumePattern, bool hasResumePattern, HIRTypeRef yieldTy, HIRExprNodeP code, bool isMove, bool isPinned, bool isCoroutineClosureBody)
            : HIRExprNode(mv$(sp))
            , returnType(::std::move(rv))
            , resumeTy(resumeTy)
            , resumePattern(::std::move(resumePattern))
            , hasResumePattern(hasResumePattern)
            , yieldTy(yieldTy)
            , code(::std::move(code))
            , isMove(isMove)
            , isPinned(isPinned)
            , isCoroutineClosureBody(isCoroutineClosureBody)
        {
        }

        HIRExprNodeGeneratorWrapper::HIRExprNodeGeneratorWrapper(Span sp, HIRTypeRef rv, HIRTypeRef yieldTy, HIRExprNodeP code, bool isFuture)
            : HIRExprNode(mv$(sp))
            , isFuture(isFuture)
            , returnType(rv)
            , yieldTy(yieldTy)
            , code(::std::move(code))
        {
        }

        HIRExprNodeAsyncBlock::HIRExprNodeAsyncBlock(Span sp, HIRTypeRef returnType, HIRExprNodeP code, bool isMove, bool isUse)
            : HIRExprNode(mv$(sp))
            , returnType(returnType)
            , code(std::move(code))
            , isMove(isMove)
            , isUse(isUse)
        {
        }

        HIRExprVisitorDef::HIRExprVisitorDef(HIRTypeInterner& types)
            : types(types)
        {
        }

        ::std::ostream& operator<<(::std::ostream& os, const HIRValueUsage& x) {
            switch (x) {
                case HIRValueUsage::Unknown:
                    os << "Unknown";
                    break;
                case HIRValueUsage::Borrow:
                    os << "Borrow";
                    break;
                case HIRValueUsage::Mutate:
                    os << "Mutate";
                    break;
                case HIRValueUsage::Move:
                    os << "Move";
                    break;
            }
            return os;
        }
