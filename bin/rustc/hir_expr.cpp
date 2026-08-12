#include "hir_expr.h"

::HIR::ExprNode::~ExprNode() {
}

unsigned int ::HIR::ExprNodeBlock::node_kind() const { return ::HIR::ExprNodeBlock::kind; }
unsigned int ::HIR::ExprNodeConstBlock::node_kind() const { return ::HIR::ExprNodeConstBlock::kind; }
unsigned int ::HIR::ExprNodeAsm::node_kind() const { return ::HIR::ExprNodeAsm::kind; }
unsigned int ::HIR::ExprNodeAsm2::node_kind() const { return ::HIR::ExprNodeAsm2::kind; }
unsigned int ::HIR::ExprNodeReturn::node_kind() const { return ::HIR::ExprNodeReturn::kind; }
unsigned int ::HIR::ExprNodeYield::node_kind() const { return ::HIR::ExprNodeYield::kind; }
unsigned int ::HIR::ExprNodeAWait::node_kind() const { return ::HIR::ExprNodeAWait::kind; }
unsigned int ::HIR::ExprNodeLoop::node_kind() const { return ::HIR::ExprNodeLoop::kind; }
unsigned int ::HIR::ExprNodeLoopControl::node_kind() const { return ::HIR::ExprNodeLoopControl::kind; }
unsigned int ::HIR::ExprNodeLet::node_kind() const { return ::HIR::ExprNodeLet::kind; }
unsigned int ::HIR::ExprNodeMatch::node_kind() const { return ::HIR::ExprNodeMatch::kind; }
unsigned int ::HIR::ExprNodeAssign::node_kind() const { return ::HIR::ExprNodeAssign::kind; }
unsigned int ::HIR::ExprNodeBinOp::node_kind() const { return ::HIR::ExprNodeBinOp::kind; }
unsigned int ::HIR::ExprNodeUniOp::node_kind() const { return ::HIR::ExprNodeUniOp::kind; }
unsigned int ::HIR::ExprNodeBorrow::node_kind() const { return ::HIR::ExprNodeBorrow::kind; }
unsigned int ::HIR::ExprNodeRawBorrow::node_kind() const { return ::HIR::ExprNodeRawBorrow::kind; }
unsigned int ::HIR::ExprNodeCast::node_kind() const { return ::HIR::ExprNodeCast::kind; }
unsigned int ::HIR::ExprNodeUnsize::node_kind() const { return ::HIR::ExprNodeUnsize::kind; }
unsigned int ::HIR::ExprNodeIndex::node_kind() const { return ::HIR::ExprNodeIndex::kind; }
unsigned int ::HIR::ExprNodeDeref::node_kind() const { return ::HIR::ExprNodeDeref::kind; }
unsigned int ::HIR::ExprNodeEmplace::node_kind() const { return ::HIR::ExprNodeEmplace::kind; }
unsigned int ::HIR::ExprNodeTupleVariant::node_kind() const { return ::HIR::ExprNodeTupleVariant::kind; }
unsigned int ::HIR::ExprNodeCallPath::node_kind() const { return ::HIR::ExprNodeCallPath::kind; }
unsigned int ::HIR::ExprNodeCallValue::node_kind() const { return ::HIR::ExprNodeCallValue::kind; }
unsigned int ::HIR::ExprNodeCallMethod::node_kind() const { return ::HIR::ExprNodeCallMethod::kind; }
unsigned int ::HIR::ExprNodeField::node_kind() const { return ::HIR::ExprNodeField::kind; }
unsigned int ::HIR::ExprNodeLiteral::node_kind() const { return ::HIR::ExprNodeLiteral::kind; }
unsigned int ::HIR::ExprNodeUnitVariant::node_kind() const { return ::HIR::ExprNodeUnitVariant::kind; }
unsigned int ::HIR::ExprNodePathValue::node_kind() const { return ::HIR::ExprNodePathValue::kind; }
unsigned int ::HIR::ExprNodeVariable::node_kind() const { return ::HIR::ExprNodeVariable::kind; }
unsigned int ::HIR::ExprNodeConstParam::node_kind() const { return ::HIR::ExprNodeConstParam::kind; }
unsigned int ::HIR::ExprNodeStructLiteral::node_kind() const { return ::HIR::ExprNodeStructLiteral::kind; }
unsigned int ::HIR::ExprNodeTuple::node_kind() const { return ::HIR::ExprNodeTuple::kind; }
unsigned int ::HIR::ExprNodeArrayList::node_kind() const { return ::HIR::ExprNodeArrayList::kind; }
unsigned int ::HIR::ExprNodeArraySized::node_kind() const { return ::HIR::ExprNodeArraySized::kind; }
unsigned int ::HIR::ExprNodeClosure::node_kind() const { return ::HIR::ExprNodeClosure::kind; }
unsigned int ::HIR::ExprNodeGenerator::node_kind() const { return ::HIR::ExprNodeGenerator::kind; }
unsigned int ::HIR::ExprNodeGeneratorWrapper::node_kind() const { return ::HIR::ExprNodeGeneratorWrapper::kind; }
unsigned int ::HIR::ExprNodeAsyncBlock::node_kind() const { return ::HIR::ExprNodeAsyncBlock::kind; }

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

void ::HIR::ExprVisitor::visit_node_ptr(::HIR::ExprNodeP& node_ptr) {
    assert(node_ptr);
    node_ptr->visit(*this);
}

void ::HIR::ExprVisitor::visit_node(::HIR::ExprNode& node) {
}

void ::HIR::ExprVisitorDef::visit_node_ptr(::HIR::ExprNodeP& node_ptr) {
    assert(node_ptr);
    TRACE_FUNCTION_F(&*node_ptr << " " << node_ptr->type_name());
    node_ptr->visit(*this);
    visit_type(node_ptr->m_res_type);
}

DEF_VISIT_H(ExprNodeBlock, node) {
    TRACE_FUNCTION_F("_Block");
    for (auto& subnode : node.m_nodes) {
        visit_node_ptr(subnode);
    }
    if (node.m_value_node) {
        visit_node_ptr(node.m_value_node);
    }
}

DEF_VISIT_H(ExprNodeConstBlock, node) {
    TRACE_FUNCTION_F("_ConstBlock");
    visit_node_ptr(node.m_inner);
}

DEF_VISIT_H(ExprNodeAsm, node) {
    TRACE_FUNCTION_F("_Asm");
    for (auto& v : node.m_outputs) {
        visit_node_ptr(v.value);
    }
    for (auto& v : node.m_inputs) {
        visit_node_ptr(v.value);
    }
}

DEF_VISIT_H(ExprNodeAsm2, node) {
    TRACE_FUNCTION_F("_Asm2");
    for (auto& v : node.m_params) {
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
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNodeYield, node) {
    TRACE_FUNCTION_F("_Yield");
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNodeAWait, node) {
    TRACE_FUNCTION_F("_AWait");
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNodeLet, node) {
    TRACE_FUNCTION_F("_Let: " << node.m_pattern);
    // Visit the value FIRST as it's evaluated before the variable is defined
    if (node.m_value) {
        visit_node_ptr(node.m_value);
    }
    visit_pattern(node.span(), node.m_pattern);
    visit_type(node.m_type);
}

DEF_VISIT_H(ExprNodeLoop, node) {
    TRACE_FUNCTION_F("_Loop");
    visit_node_ptr(node.m_code);
}

DEF_VISIT_H(ExprNodeLoopControl, node) {
    TRACE_FUNCTION_F("_LoopControl");
    if (node.m_value) {
        visit_node_ptr(node.m_value);
    }
}

DEF_VISIT_H(ExprNodeMatch, node) {
    TRACE_FUNCTION_F("_Match");
    visit_node_ptr(node.m_value);
    for (auto& arm : node.m_arms) {
        for (auto& pat : arm.m_patterns) {
            visit_pattern(node.span(), pat);
        }
        for (auto& c : arm.m_guards) {
            visit_pattern(node.span(), c.pat);
            visit_node_ptr(c.val);
        }
        visit_node_ptr(arm.m_code);
    }
}

DEF_VISIT(ExprNodeAssign, node, TRACE_FUNCTION_F("_Assign"); visit_node_ptr(node.m_slot); visit_node_ptr(node.m_value);)
DEF_VISIT(ExprNodeBinOp, node, TRACE_FUNCTION_F("_BinOp"); visit_node_ptr(node.m_left); visit_node_ptr(node.m_right);)
DEF_VISIT(ExprNodeUniOp, node, TRACE_FUNCTION_F("_UniOp"); visit_node_ptr(node.m_value);)
DEF_VISIT(ExprNodeBorrow, node, TRACE_FUNCTION_F("_Borrow"); visit_node_ptr(node.m_value);)
DEF_VISIT(ExprNodeRawBorrow, node, visit_node_ptr(node.m_value);)

DEF_VISIT_H(ExprNodeCast, node) {
    TRACE_FUNCTION_F("_Cast " << node.m_dst_type);
    visit_type(node.m_dst_type);
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNodeUnsize, node) {
    TRACE_FUNCTION_F("_Unsize " << node.m_dst_type);
    visit_type(node.m_dst_type);
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNodeIndex, node) {
    TRACE_FUNCTION_F("_Index");
    visit_node_ptr(node.m_value);
    visit_node_ptr(node.m_index);
}

DEF_VISIT_H(ExprNodeDeref, node) {
    TRACE_FUNCTION_F("_Deref");
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNodeEmplace, node) {
    TRACE_FUNCTION_F("_Emplace");
    if (node.m_place) {
        visit_node_ptr(node.m_place);
    }
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNodeTupleVariant, node) {
    TRACE_FUNCTION_F("_TupleVariant: " << node.m_path);
    visit_generic_path(::HIR::Visitor::PathContext::VALUE, node.m_path);

    for (auto& ty : node.m_arg_types) {
        if (ty != HIR::TypeRef()) {
            visit_type(ty);
        }
    }

    for (auto& arg : node.m_args) {
        visit_node_ptr(arg);
    }
}

DEF_VISIT_H(ExprNodeCallPath, node) {
    TRACE_FUNCTION_F("_CallPath: " << node.m_path);
    for (auto& ty : node.m_cache.m_arg_types) {
        visit_type(ty);
    }

    visit_path(::HIR::Visitor::PathContext::VALUE, node.m_path);
    for (auto& arg : node.m_args) {
        visit_node_ptr(arg);
    }
}

DEF_VISIT_H(ExprNodeCallValue, node) {
    TRACE_FUNCTION_F("_CallValue:");
    for (auto& ty : node.m_arg_types) {
        visit_type(ty);
    }

    visit_node_ptr(node.m_value);
    for (auto& arg : node.m_args) {
        visit_node_ptr(arg);
    }
}

DEF_VISIT_H(ExprNodeCallMethod, node) {
    TRACE_FUNCTION_FR("_CallMethod: " << node.m_method, "_CallMethod: " << node.m_method);
    visit_path_params(node.m_params);
    for (auto& ty : node.m_cache.m_arg_types) {
        visit_type(ty);
    }

    visit_path(::HIR::Visitor::PathContext::VALUE, node.m_method_path);

    visit_node_ptr(node.m_value);
    for (auto& arg : node.m_args) {
        visit_node_ptr(arg);
    }
}

DEF_VISIT_H(ExprNodeField, node) {
    TRACE_FUNCTION_F("_Field: " << node.m_field);
    visit_node_ptr(node.m_value);
}

DEF_VISIT(ExprNodeLiteral, node, TRACE_FUNCTION_F("_Literal");)
DEF_VISIT(ExprNodeUnitVariant, node, TRACE_FUNCTION_F("_UnitVariant: " << node.m_path); visit_generic_path(::HIR::Visitor::PathContext::VALUE, node.m_path);)
DEF_VISIT(ExprNodePathValue, node, TRACE_FUNCTION_F("_PathValue: " << node.m_path); visit_path(::HIR::Visitor::PathContext::VALUE, node.m_path);)
DEF_VISIT(ExprNodeVariable, node, TRACE_FUNCTION_F("_Variable: #" << node.m_slot);)
DEF_VISIT(ExprNodeConstParam, node, TRACE_FUNCTION_F("_ConstParam");)

DEF_VISIT_H(ExprNodeStructLiteral, node) {
    TRACE_FUNCTION_F("_StructLiteral: " << node.m_real_path);
    if (node.m_type != HIR::TypeRef()) {
        visit_type(node.m_type);
    }
    if (node.m_base_value) {
        visit_node_ptr(node.m_base_value);
    }
    for (auto& val : node.m_values) {
        visit_node_ptr(val.second);
    }

    visit_generic_path(::HIR::Visitor::PathContext::TYPE, node.m_real_path);
}

DEF_VISIT_H(ExprNodeTuple, node) {
    TRACE_FUNCTION_F("_Tuple");
    for (auto& val : node.m_vals) {
        visit_node_ptr(val);
    }
}

DEF_VISIT_H(ExprNodeArrayList, node) {
    TRACE_FUNCTION_F("_ArrayList");
    for (auto& val : node.m_vals) {
        visit_node_ptr(val);
    }
}
DEF_VISIT(
    ExprNodeArraySized, node, TRACE_FUNCTION_F("_ArraySized"); visit_node_ptr(node.m_val);
    //visit_arraysize(node.m_size); // Don't do this, array sizes are not part of the normal expression tree
)

DEF_VISIT_H(ExprNodeClosure, node) {
    TRACE_FUNCTION_F("_Closure");
    if (node.m_obj_path != HIR::GenericPath()) {
        for (auto& cap : node.m_captures) {
            visit_node_ptr(cap);
        }
    } else {
        for (auto& arg : node.m_args) {
            visit_pattern(node.span(), arg.first);
            visit_type(arg.second);
        }
        visit_type(node.m_return);
        visit_node_ptr(node.m_code);
    }
}

namespace HIR {
    ::std::ostream& operator<<(::std::ostream& os, const ExprNodeClosure::AvuCache::Capture& x) {
        os << "#" << x.root_slot;
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
    visit_type(node.m_return);
    visit_type(node.m_yield_ty);
    visit_type(node.m_resume_ty);
    if (node.m_code) {
        visit_node_ptr(node.m_code);
    } else {
        for (auto& cap : node.m_captures) {
            visit_node_ptr(cap);
        }
    }
}

DEF_VISIT_H(ExprNodeGeneratorWrapper, node) {
    //for(auto& arg : node.m_args) {
    //    visit_pattern(node.span(), arg.first);
    //    visit_type(arg.second);
    //}
    visit_type(node.m_return);
    visit_type(node.m_yield_ty);
    if (node.m_code) {
        visit_node_ptr(node.m_code);
    }
}

DEF_VISIT_H(ExprNodeAsyncBlock, node) {
    TRACE_FUNCTION_F("_AsyncBlock");
    if (node.m_code) {
        visit_node_ptr(node.m_code);
    } else {
    }
}

#undef DEF_VISIT
#undef DEF_VISIT_H

// TODO: Merge this with the stuff in ::HIR::Visitor
void ::HIR::ExprVisitorDef::visit_pattern(const Span& sp, ::HIR::Pattern& pat) {
    TU_MATCH_HDRA( (pat.m_data), {)
    TU_ARMA(Any, e) {
        }
        TU_ARMA(Box, e) {
            this->visit_pattern(sp, *e.sub);
        }
        TU_ARMA(Ref, e) {
            this->visit_pattern(sp, *e.sub);
        }
        TU_ARMA(Tuple, e) {
            for (auto& subpat : e.sub_patterns) {
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
            for (auto& fld_pat : e.sub_patterns) {
                this->visit_pattern(sp, fld_pat.second);
            }
        }
        TU_ARMA(Value, e) {
        }
        TU_ARMA(Range, e) {
        }
        TU_ARMA(Slice, e) {
            for (auto& subpat : e.sub_patterns) {
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
    auto data = ty->clone_data();
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
        this->visit_trait_path(e.m_trait);
        for(auto& trait : e.m_markers) {
        this->visit_generic_path(::HIR::Visitor::PathContext::TYPE, trait);
        }
        ),
    (ErasedType,
        for(auto& trait : e.m_traits) {
        this->visit_trait_path(trait);
        }
        TU_MATCH_HDRA( (e.m_inner), {)
        TU_ARMA(Known, ee) {
            this->visit_type(ee);
}

TU_ARMA(Fcn, ee) {
    this->visit_path(::HIR::Visitor::PathContext::TYPE, ee.m_origin);
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
        for(auto& t : e.m_arg_types) {
    this->visit_type(t);
        }
        this->visit_type(e.m_rettype);
        ),
    (NodeType,
        )
    )
    ty = m_types.intern(std::move(data));
        }

        void ::HIR::ExprVisitorDef::visit_path_params(::HIR::PathParams& pp) {
            for (auto& ty : pp.m_types) {
                visit_type(ty);
            }
        }

        void ::HIR::ExprVisitorDef::visit_trait_path(::HIR::TraitPath& p) {
            this->visit_generic_path(::HIR::Visitor::PathContext::TYPE, p.m_path);
            for (auto& assoc : p.m_type_bounds) {
                this->visit_type(assoc.second.type);
            }
            for (auto& assoc : p.m_trait_bounds) {
                for (auto& t : assoc.second.traits) {
                    this->visit_trait_path(t);
                }
            }
        }

        void ::HIR::ExprVisitorDef::visit_path(::HIR::Visitor::PathContext pc, ::HIR::Path& path) {
            TU_MATCHA((path.m_data), (e), (Generic, visit_generic_path(pc, e);), (UfcsKnown, visit_type(e.type); visit_generic_path(pc, e.trait); visit_path_params(e.params);), (UfcsUnknown, visit_type(e.type); visit_path_params(e.params);), (UfcsInherent, visit_type(e.type); visit_path_params(e.params); visit_path_params(e.impl_params);))
        }

        void ::HIR::ExprVisitorDef::visit_generic_path(::HIR::Visitor::PathContext pc, ::HIR::GenericPath& path) {
            visit_path_params(path.m_params);
        }

namespace HIR {

ExprNode::ExprNode(Span sp)
    : m_span(mv$(sp)) {
}
ExprNodeBlock::ExprNodeBlock(Span sp)
    : ExprNode(mv$(sp))
    , m_is_unsafe(false) {
}
ExprNodeBlock::ExprNodeBlock(Span sp, bool is_unsafe, ::std::vector<ExprNodeP> nodes, ExprNodeP value_node)
    : ExprNode(mv$(sp))
    , m_is_unsafe(is_unsafe)
    , m_nodes(mv$(nodes))
    , m_value_node(mv$(value_node)) {
}
ExprNodeConstBlock::ExprNodeConstBlock(Span sp, ExprNodeP inner)
    : ExprNode(mv$(sp))
    , m_inner(mv$(inner)) {
}
ExprNodeAsm2::ExprNodeAsm2(Span sp, AsmCommon::Options options, std::vector<AsmCommon::Line> lines, std::vector<Param> params)
    : ExprNode(mv$(sp))
    , m_options(options)
    , m_lines(::std::move(lines))
    , m_params(::std::move(params)) {
}
ExprNodeReturn::ExprNodeReturn(Span sp, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_value(mv$(value)) {
}
ExprNodeYield::ExprNodeYield(Span sp, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_value(mv$(value)) {
}
ExprNodeAWait::ExprNodeAWait(Span sp, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_value(mv$(value)) {
}
ExprNodeLoop::ExprNodeLoop(Span sp, RcString label, ::HIR::ExprNodeP code, bool require_label)
    : ExprNode(mv$(sp))
    , m_label(mv$(label))
    , m_code(mv$(code))
    , m_require_label(require_label) {
}
// populated by expr_cs__enum.cpp

ExprNodeLoopControl::ExprNodeLoopControl(Span sp, RcString label, bool cont, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_label(mv$(label))
    , m_continue(cont)
    , m_value(mv$(value))
    , m_target_node(nullptr) {
}
ExprNodeLet::ExprNodeLet(Span sp, ::HIR::Pattern pat, ::HIR::TypeRef ty, ::HIR::ExprNodeP val, bool is_super)
    : ExprNode(mv$(sp))
    , m_pattern(mv$(pat))
    , m_type(mv$(ty))
    , m_value(mv$(val))
    , m_is_super(is_super) {
}
ExprNodeMatch::ExprNodeMatch(Span sp, ::HIR::ExprNodeP val, ::std::vector<Arm> arms, bool is_let_else)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_arms(mv$(arms))
    , m_is_let_else(is_let_else) {
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
    , m_op(op)
    , m_slot(mv$(slot))
    , m_value(mv$(value)) {
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
    , m_op(op)
    , m_left(mv$(left))
    , m_right(mv$(right)) {
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
    , m_op(op)
    , m_value(mv$(value)) {
}
ExprNodeBorrow::ExprNodeBorrow(Span sp, ::HIR::BorrowType bt, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_type(bt)
    , m_value(mv$(value))
    , m_is_valid_static_borrow_constant(false) {
}
ExprNodeRawBorrow::ExprNodeRawBorrow(Span sp, ::HIR::BorrowType bt, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_type(bt)
    , m_value(mv$(value)) {
}
ExprNodeCast::ExprNodeCast(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dst_type)
    : ExprNode(mv$(sp))
    , m_value(mv$(value))
    , m_dst_type(mv$(dst_type)) {
}
ExprNodeUnsize::ExprNodeUnsize(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dst_type)
    : ExprNode(mv$(sp))
    , m_value(mv$(value))
    , m_dst_type(mv$(dst_type)) {
}
ExprNodeIndex::ExprNodeIndex(Span sp, ::HIR::ExprNodeP val, ::HIR::ExprNodeP index)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_index(mv$(index)) {
}
ExprNodeDeref::ExprNodeDeref(Span sp, ::HIR::ExprNodeP val)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_trait_used(TraitUsed::Unknown) {
}
ExprNodeEmplace::ExprNodeEmplace(Span sp, Type ty, ::HIR::ExprNodeP place, ::HIR::ExprNodeP val)
    : ExprNode(mv$(sp))
    , m_type(ty)
    , m_place(mv$(place))
    , m_value(mv$(val)) {
}
ExprNodeTupleVariant::ExprNodeTupleVariant(Span sp, ::HIR::GenericPath path, bool is_struct, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , m_path(mv$(path))
    , m_is_struct(is_struct)
    , m_args(mv$(args)) {
}
ExprNodeCallPath::ExprNodeCallPath(Span sp, ::HIR::Path path, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , m_path(mv$(path))
    , m_args(mv$(args)) {
}
ExprNodeCallValue::ExprNodeCallValue(Span sp, ::HIR::ExprNodeP val, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_args(mv$(args)) {
}
ExprNodeCallMethod::ExprNodeCallMethod(Span sp, ::HIR::ExprNodeP val, RcString method_name, ::HIR::PathParams params, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_method(mv$(method_name))
    , m_params(mv$(params))
    , m_args(mv$(args))
    ,

    m_method_path(::HIR::SimplePath("", {})) {
}
ExprNodeField::ExprNodeField(Span sp, ::HIR::ExprNodeP val, RcString field)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_field(mv$(field)) {
}
ExprNodeLiteral::ExprNodeLiteral(Span sp, Data data)
    : ExprNode(mv$(sp))
    , m_data(mv$(data)) {
}
ExprNodeUnitVariant::ExprNodeUnitVariant(Span sp, ::HIR::GenericPath path, bool is_struct)
    : ExprNode(mv$(sp))
    , m_path(mv$(path))
    , m_is_struct(is_struct) {
}
ExprNodePathValue::ExprNodePathValue(Span sp, ::HIR::Path path, Target target)
    : ExprNode(mv$(sp))
    , m_path(mv$(path))
    , m_target(target) {
}
ExprNodeVariable::ExprNodeVariable(Span sp, RcString name, unsigned int slot)
    : ExprNode(mv$(sp))
    , m_name(mv$(name))
    , m_slot(slot) {
}
ExprNodeConstParam::ExprNodeConstParam(Span sp, RcString name, unsigned int binding)
    : ExprNode(mv$(sp))
    , m_name(mv$(name))
    , m_binding(binding) {
}
ExprNodeStructLiteral::ExprNodeStructLiteral(Span sp, ::HIR::TypeRef ty, bool is_struct, ::HIR::ExprNodeP base_value, t_values values)
    : ExprNode(mv$(sp))
    , m_type(mv$(ty))
    , m_is_struct(is_struct)
    , m_base_value(mv$(base_value))
    , m_values(mv$(values)) {
}
ExprNodeStructLiteral::ExprNodeStructLiteral(Span sp, ::HIR::TypeRef ty, bool is_struct, bool, t_values values)
    : ExprNode(mv$(sp))
    , m_type(mv$(ty))
    , m_is_struct(is_struct)
    , m_use_defaults(true)
    , m_values(mv$(values)) {
}
ExprNodeTuple::ExprNodeTuple(Span sp, ::std::vector<::HIR::ExprNodeP> vals)
    : ExprNode(mv$(sp))
    , m_vals(mv$(vals)) {
}
ExprNodeArrayList::ExprNodeArrayList(Span sp, ::std::vector<::HIR::ExprNodeP> vals)
    : ExprNode(mv$(sp))
    , m_vals(mv$(vals)) {
}
ExprNodeArraySized::ExprNodeArraySized(Span sp, ::HIR::ExprNodeP val, ::HIR::ExprPtr size)
    : ExprNode(mv$(sp))
    , m_val(mv$(val))
    , m_size(HIR::ConstGeneric(std::make_unique<HIR::ConstGenericUnevaluated>(mv$(size)))) {
}
ExprNodeClosure::ExprNodeClosure(Span sp, args_t args, ::HIR::TypeRef rv, ::HIR::ExprNodeP code, bool is_move)
    : ExprNode(mv$(sp))
    , m_args(::std::move(args))
    , m_return(::std::move(rv))
    , m_code(::std::move(code))
    , m_is_move(is_move) {
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
    , m_return(::std::move(rv))
    , m_resume_ty(resume_ty)
    , m_yield_ty(yield_ty)
    , m_code(::std::move(code))
    , m_is_move(is_move)
    , m_is_pinned(is_pinned) {
}
ExprNodeGeneratorWrapper::ExprNodeGeneratorWrapper(
    Span sp,
    ::HIR::TypeRef rv,
    ::HIR::TypeRef yield_ty,
    ::HIR::ExprNodeP code,
    bool is_future
)
    : ExprNode(mv$(sp))
    , m_is_future(is_future)
    , m_return(rv)
    , m_yield_ty(yield_ty)
    , m_code(::std::move(code)) {
}
ExprNodeAsyncBlock::ExprNodeAsyncBlock(Span sp, ::HIR::ExprNodeP code, bool is_move)
    : ExprNode(mv$(sp))
    , m_code(std::move(code))
    , m_is_move(is_move) {
}
ExprVisitorDef::ExprVisitorDef(TypeInterner& types): m_types(types) {}
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
