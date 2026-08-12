#include "hir_expr.h"

::HIR::ExprNode::~ExprNode() {
}

unsigned int ::HIR::ExprNode_Block::node_kind() const { return ::HIR::ExprNode_Block::kind; }
unsigned int ::HIR::ExprNode_ConstBlock::node_kind() const { return ::HIR::ExprNode_ConstBlock::kind; }
unsigned int ::HIR::ExprNode_Asm::node_kind() const { return ::HIR::ExprNode_Asm::kind; }
unsigned int ::HIR::ExprNode_Asm2::node_kind() const { return ::HIR::ExprNode_Asm2::kind; }
unsigned int ::HIR::ExprNode_Return::node_kind() const { return ::HIR::ExprNode_Return::kind; }
unsigned int ::HIR::ExprNode_Yield::node_kind() const { return ::HIR::ExprNode_Yield::kind; }
unsigned int ::HIR::ExprNode_AWait::node_kind() const { return ::HIR::ExprNode_AWait::kind; }
unsigned int ::HIR::ExprNode_Loop::node_kind() const { return ::HIR::ExprNode_Loop::kind; }
unsigned int ::HIR::ExprNode_LoopControl::node_kind() const { return ::HIR::ExprNode_LoopControl::kind; }
unsigned int ::HIR::ExprNode_Let::node_kind() const { return ::HIR::ExprNode_Let::kind; }
unsigned int ::HIR::ExprNode_Match::node_kind() const { return ::HIR::ExprNode_Match::kind; }
unsigned int ::HIR::ExprNode_Assign::node_kind() const { return ::HIR::ExprNode_Assign::kind; }
unsigned int ::HIR::ExprNode_BinOp::node_kind() const { return ::HIR::ExprNode_BinOp::kind; }
unsigned int ::HIR::ExprNode_UniOp::node_kind() const { return ::HIR::ExprNode_UniOp::kind; }
unsigned int ::HIR::ExprNode_Borrow::node_kind() const { return ::HIR::ExprNode_Borrow::kind; }
unsigned int ::HIR::ExprNode_RawBorrow::node_kind() const { return ::HIR::ExprNode_RawBorrow::kind; }
unsigned int ::HIR::ExprNode_Cast::node_kind() const { return ::HIR::ExprNode_Cast::kind; }
unsigned int ::HIR::ExprNode_Unsize::node_kind() const { return ::HIR::ExprNode_Unsize::kind; }
unsigned int ::HIR::ExprNode_Index::node_kind() const { return ::HIR::ExprNode_Index::kind; }
unsigned int ::HIR::ExprNode_Deref::node_kind() const { return ::HIR::ExprNode_Deref::kind; }
unsigned int ::HIR::ExprNode_Emplace::node_kind() const { return ::HIR::ExprNode_Emplace::kind; }
unsigned int ::HIR::ExprNode_TupleVariant::node_kind() const { return ::HIR::ExprNode_TupleVariant::kind; }
unsigned int ::HIR::ExprNode_CallPath::node_kind() const { return ::HIR::ExprNode_CallPath::kind; }
unsigned int ::HIR::ExprNode_CallValue::node_kind() const { return ::HIR::ExprNode_CallValue::kind; }
unsigned int ::HIR::ExprNode_CallMethod::node_kind() const { return ::HIR::ExprNode_CallMethod::kind; }
unsigned int ::HIR::ExprNode_Field::node_kind() const { return ::HIR::ExprNode_Field::kind; }
unsigned int ::HIR::ExprNode_Literal::node_kind() const { return ::HIR::ExprNode_Literal::kind; }
unsigned int ::HIR::ExprNode_UnitVariant::node_kind() const { return ::HIR::ExprNode_UnitVariant::kind; }
unsigned int ::HIR::ExprNode_PathValue::node_kind() const { return ::HIR::ExprNode_PathValue::kind; }
unsigned int ::HIR::ExprNode_Variable::node_kind() const { return ::HIR::ExprNode_Variable::kind; }
unsigned int ::HIR::ExprNode_ConstParam::node_kind() const { return ::HIR::ExprNode_ConstParam::kind; }
unsigned int ::HIR::ExprNode_StructLiteral::node_kind() const { return ::HIR::ExprNode_StructLiteral::kind; }
unsigned int ::HIR::ExprNode_Tuple::node_kind() const { return ::HIR::ExprNode_Tuple::kind; }
unsigned int ::HIR::ExprNode_ArrayList::node_kind() const { return ::HIR::ExprNode_ArrayList::kind; }
unsigned int ::HIR::ExprNode_ArraySized::node_kind() const { return ::HIR::ExprNode_ArraySized::kind; }
unsigned int ::HIR::ExprNode_Closure::node_kind() const { return ::HIR::ExprNode_Closure::kind; }
unsigned int ::HIR::ExprNode_Generator::node_kind() const { return ::HIR::ExprNode_Generator::kind; }
unsigned int ::HIR::ExprNode_GeneratorWrapper::node_kind() const { return ::HIR::ExprNode_GeneratorWrapper::kind; }
unsigned int ::HIR::ExprNode_AsyncBlock::node_kind() const { return ::HIR::ExprNode_AsyncBlock::kind; }

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

DEF_VISIT_H(ExprNode_Block, node) {
    TRACE_FUNCTION_F("_Block");
    for (auto& subnode : node.m_nodes) {
        visit_node_ptr(subnode);
    }
    if (node.m_value_node) {
        visit_node_ptr(node.m_value_node);
    }
}

DEF_VISIT_H(ExprNode_ConstBlock, node) {
    TRACE_FUNCTION_F("_ConstBlock");
    visit_node_ptr(node.m_inner);
}

DEF_VISIT_H(ExprNode_Asm, node) {
    TRACE_FUNCTION_F("_Asm");
    for (auto& v : node.m_outputs) {
        visit_node_ptr(v.value);
    }
    for (auto& v : node.m_inputs) {
        visit_node_ptr(v.value);
    }
}

DEF_VISIT_H(ExprNode_Asm2, node) {
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

DEF_VISIT_H(ExprNode_Return, node) {
    TRACE_FUNCTION_F("_Return");
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNode_Yield, node) {
    TRACE_FUNCTION_F("_Yield");
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNode_AWait, node) {
    TRACE_FUNCTION_F("_AWait");
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNode_Let, node) {
    TRACE_FUNCTION_F("_Let: " << node.m_pattern);
    // Visit the value FIRST as it's evaluated before the variable is defined
    if (node.m_value) {
        visit_node_ptr(node.m_value);
    }
    visit_pattern(node.span(), node.m_pattern);
    visit_type(node.m_type);
}

DEF_VISIT_H(ExprNode_Loop, node) {
    TRACE_FUNCTION_F("_Loop");
    visit_node_ptr(node.m_code);
}

DEF_VISIT_H(ExprNode_LoopControl, node) {
    TRACE_FUNCTION_F("_LoopControl");
    if (node.m_value) {
        visit_node_ptr(node.m_value);
    }
}

DEF_VISIT_H(ExprNode_Match, node) {
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

DEF_VISIT(ExprNode_Assign, node, TRACE_FUNCTION_F("_Assign"); visit_node_ptr(node.m_slot); visit_node_ptr(node.m_value);)
DEF_VISIT(ExprNode_BinOp, node, TRACE_FUNCTION_F("_BinOp"); visit_node_ptr(node.m_left); visit_node_ptr(node.m_right);)
DEF_VISIT(ExprNode_UniOp, node, TRACE_FUNCTION_F("_UniOp"); visit_node_ptr(node.m_value);)
DEF_VISIT(ExprNode_Borrow, node, TRACE_FUNCTION_F("_Borrow"); visit_node_ptr(node.m_value);)
DEF_VISIT(ExprNode_RawBorrow, node, visit_node_ptr(node.m_value);)

DEF_VISIT_H(ExprNode_Cast, node) {
    TRACE_FUNCTION_F("_Cast " << node.m_dst_type);
    visit_type(node.m_dst_type);
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNode_Unsize, node) {
    TRACE_FUNCTION_F("_Unsize " << node.m_dst_type);
    visit_type(node.m_dst_type);
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNode_Index, node) {
    TRACE_FUNCTION_F("_Index");
    visit_node_ptr(node.m_value);
    visit_node_ptr(node.m_index);
}

DEF_VISIT_H(ExprNode_Deref, node) {
    TRACE_FUNCTION_F("_Deref");
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNode_Emplace, node) {
    TRACE_FUNCTION_F("_Emplace");
    if (node.m_place) {
        visit_node_ptr(node.m_place);
    }
    visit_node_ptr(node.m_value);
}

DEF_VISIT_H(ExprNode_TupleVariant, node) {
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

DEF_VISIT_H(ExprNode_CallPath, node) {
    TRACE_FUNCTION_F("_CallPath: " << node.m_path);
    for (auto& ty : node.m_cache.m_arg_types) {
        visit_type(ty);
    }

    visit_path(::HIR::Visitor::PathContext::VALUE, node.m_path);
    for (auto& arg : node.m_args) {
        visit_node_ptr(arg);
    }
}

DEF_VISIT_H(ExprNode_CallValue, node) {
    TRACE_FUNCTION_F("_CallValue:");
    for (auto& ty : node.m_arg_types) {
        visit_type(ty);
    }

    visit_node_ptr(node.m_value);
    for (auto& arg : node.m_args) {
        visit_node_ptr(arg);
    }
}

DEF_VISIT_H(ExprNode_CallMethod, node) {
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

DEF_VISIT_H(ExprNode_Field, node) {
    TRACE_FUNCTION_F("_Field: " << node.m_field);
    visit_node_ptr(node.m_value);
}

DEF_VISIT(ExprNode_Literal, node, TRACE_FUNCTION_F("_Literal");)
DEF_VISIT(ExprNode_UnitVariant, node, TRACE_FUNCTION_F("_UnitVariant: " << node.m_path); visit_generic_path(::HIR::Visitor::PathContext::VALUE, node.m_path);)
DEF_VISIT(ExprNode_PathValue, node, TRACE_FUNCTION_F("_PathValue: " << node.m_path); visit_path(::HIR::Visitor::PathContext::VALUE, node.m_path);)
DEF_VISIT(ExprNode_Variable, node, TRACE_FUNCTION_F("_Variable: #" << node.m_slot);)
DEF_VISIT(ExprNode_ConstParam, node, TRACE_FUNCTION_F("_ConstParam");)

DEF_VISIT_H(ExprNode_StructLiteral, node) {
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

DEF_VISIT_H(ExprNode_Tuple, node) {
    TRACE_FUNCTION_F("_Tuple");
    for (auto& val : node.m_vals) {
        visit_node_ptr(val);
    }
}

DEF_VISIT_H(ExprNode_ArrayList, node) {
    TRACE_FUNCTION_F("_ArrayList");
    for (auto& val : node.m_vals) {
        visit_node_ptr(val);
    }
}
DEF_VISIT(
    ExprNode_ArraySized, node, TRACE_FUNCTION_F("_ArraySized"); visit_node_ptr(node.m_val);
    //visit_arraysize(node.m_size); // Don't do this, array sizes are not part of the normal expression tree
)

DEF_VISIT_H(ExprNode_Closure, node) {
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
    ::std::ostream& operator<<(::std::ostream& os, const ExprNode_Closure::AvuCache::Capture& x) {
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

DEF_VISIT_H(ExprNode_Generator, node) {
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

DEF_VISIT_H(ExprNode_GeneratorWrapper, node) {
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

DEF_VISIT_H(ExprNode_AsyncBlock, node) {
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
ExprNode_Block::ExprNode_Block(Span sp)
    : ExprNode(mv$(sp))
    , m_is_unsafe(false) {
}
ExprNode_Block::ExprNode_Block(Span sp, bool is_unsafe, ::std::vector<ExprNodeP> nodes, ExprNodeP value_node)
    : ExprNode(mv$(sp))
    , m_is_unsafe(is_unsafe)
    , m_nodes(mv$(nodes))
    , m_value_node(mv$(value_node)) {
}
ExprNode_ConstBlock::ExprNode_ConstBlock(Span sp, ExprNodeP inner)
    : ExprNode(mv$(sp))
    , m_inner(mv$(inner)) {
}
ExprNode_Asm2::ExprNode_Asm2(Span sp, AsmCommon::Options options, std::vector<AsmCommon::Line> lines, std::vector<Param> params)
    : ExprNode(mv$(sp))
    , m_options(options)
    , m_lines(::std::move(lines))
    , m_params(::std::move(params)) {
}
ExprNode_Return::ExprNode_Return(Span sp, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_value(mv$(value)) {
}
ExprNode_Yield::ExprNode_Yield(Span sp, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_value(mv$(value)) {
}
ExprNode_AWait::ExprNode_AWait(Span sp, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_value(mv$(value)) {
}
ExprNode_Loop::ExprNode_Loop(Span sp, RcString label, ::HIR::ExprNodeP code, bool require_label)
    : ExprNode(mv$(sp))
    , m_label(mv$(label))
    , m_code(mv$(code))
    , m_require_label(require_label) {
}
// populated by expr_cs__enum.cpp

ExprNode_LoopControl::ExprNode_LoopControl(Span sp, RcString label, bool cont, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_label(mv$(label))
    , m_continue(cont)
    , m_value(mv$(value))
    , m_target_node(nullptr) {
}
ExprNode_Let::ExprNode_Let(Span sp, ::HIR::Pattern pat, ::HIR::TypeRef ty, ::HIR::ExprNodeP val, bool is_super)
    : ExprNode(mv$(sp))
    , m_pattern(mv$(pat))
    , m_type(mv$(ty))
    , m_value(mv$(val))
    , m_is_super(is_super) {
}
ExprNode_Match::ExprNode_Match(Span sp, ::HIR::ExprNodeP val, ::std::vector<Arm> arms, bool is_let_else)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_arms(mv$(arms))
    , m_is_let_else(is_let_else) {
}
const char* ExprNode_Assign::opname(Op v) {
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
ExprNode_Assign::ExprNode_Assign(Span sp, Op op, ::HIR::ExprNodeP slot, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_op(op)
    , m_slot(mv$(slot))
    , m_value(mv$(value)) {
}
const char* ExprNode_BinOp::opname(Op v) {
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
ExprNode_BinOp::ExprNode_BinOp(Span sp, Op op, ::HIR::ExprNodeP left, ::HIR::ExprNodeP right)
    : ExprNode(mv$(sp))
    , m_op(op)
    , m_left(mv$(left))
    , m_right(mv$(right)) {
}
const char* ExprNode_UniOp::opname(Op v) {
    switch (v) {
        case Op::Invert:
            return "!";
        case Op::Negate:
            return "-";
    }
    throw "";
}
ExprNode_UniOp::ExprNode_UniOp(Span sp, Op op, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_op(op)
    , m_value(mv$(value)) {
}
ExprNode_Borrow::ExprNode_Borrow(Span sp, ::HIR::BorrowType bt, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_type(bt)
    , m_value(mv$(value))
    , m_is_valid_static_borrow_constant(false) {
}
ExprNode_RawBorrow::ExprNode_RawBorrow(Span sp, ::HIR::BorrowType bt, ::HIR::ExprNodeP value)
    : ExprNode(mv$(sp))
    , m_type(bt)
    , m_value(mv$(value)) {
}
ExprNode_Cast::ExprNode_Cast(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dst_type)
    : ExprNode(mv$(sp))
    , m_value(mv$(value))
    , m_dst_type(mv$(dst_type)) {
}
ExprNode_Unsize::ExprNode_Unsize(Span sp, ::HIR::ExprNodeP value, ::HIR::TypeRef dst_type)
    : ExprNode(mv$(sp))
    , m_value(mv$(value))
    , m_dst_type(mv$(dst_type)) {
}
ExprNode_Index::ExprNode_Index(Span sp, ::HIR::ExprNodeP val, ::HIR::ExprNodeP index)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_index(mv$(index)) {
}
ExprNode_Deref::ExprNode_Deref(Span sp, ::HIR::ExprNodeP val)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_trait_used(TraitUsed::Unknown) {
}
ExprNode_Emplace::ExprNode_Emplace(Span sp, Type ty, ::HIR::ExprNodeP place, ::HIR::ExprNodeP val)
    : ExprNode(mv$(sp))
    , m_type(ty)
    , m_place(mv$(place))
    , m_value(mv$(val)) {
}
ExprNode_TupleVariant::ExprNode_TupleVariant(Span sp, ::HIR::GenericPath path, bool is_struct, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , m_path(mv$(path))
    , m_is_struct(is_struct)
    , m_args(mv$(args)) {
}
ExprNode_CallPath::ExprNode_CallPath(Span sp, ::HIR::Path path, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , m_path(mv$(path))
    , m_args(mv$(args)) {
}
ExprNode_CallValue::ExprNode_CallValue(Span sp, ::HIR::ExprNodeP val, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_args(mv$(args)) {
}
ExprNode_CallMethod::ExprNode_CallMethod(Span sp, ::HIR::ExprNodeP val, RcString method_name, ::HIR::PathParams params, ::std::vector<::HIR::ExprNodeP> args)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_method(mv$(method_name))
    , m_params(mv$(params))
    , m_args(mv$(args))
    ,

    m_method_path(::HIR::SimplePath("", {})) {
}
ExprNode_Field::ExprNode_Field(Span sp, ::HIR::ExprNodeP val, RcString field)
    : ExprNode(mv$(sp))
    , m_value(mv$(val))
    , m_field(mv$(field)) {
}
ExprNode_Literal::ExprNode_Literal(Span sp, Data data)
    : ExprNode(mv$(sp))
    , m_data(mv$(data)) {
}
ExprNode_UnitVariant::ExprNode_UnitVariant(Span sp, ::HIR::GenericPath path, bool is_struct)
    : ExprNode(mv$(sp))
    , m_path(mv$(path))
    , m_is_struct(is_struct) {
}
ExprNode_PathValue::ExprNode_PathValue(Span sp, ::HIR::Path path, Target target)
    : ExprNode(mv$(sp))
    , m_path(mv$(path))
    , m_target(target) {
}
ExprNode_Variable::ExprNode_Variable(Span sp, RcString name, unsigned int slot)
    : ExprNode(mv$(sp))
    , m_name(mv$(name))
    , m_slot(slot) {
}
ExprNode_ConstParam::ExprNode_ConstParam(Span sp, RcString name, unsigned int binding)
    : ExprNode(mv$(sp))
    , m_name(mv$(name))
    , m_binding(binding) {
}
ExprNode_StructLiteral::ExprNode_StructLiteral(Span sp, ::HIR::TypeRef ty, bool is_struct, ::HIR::ExprNodeP base_value, t_values values)
    : ExprNode(mv$(sp))
    , m_type(mv$(ty))
    , m_is_struct(is_struct)
    , m_base_value(mv$(base_value))
    , m_values(mv$(values)) {
}
ExprNode_StructLiteral::ExprNode_StructLiteral(Span sp, ::HIR::TypeRef ty, bool is_struct, bool, t_values values)
    : ExprNode(mv$(sp))
    , m_type(mv$(ty))
    , m_is_struct(is_struct)
    , m_use_defaults(true)
    , m_values(mv$(values)) {
}
ExprNode_Tuple::ExprNode_Tuple(Span sp, ::std::vector<::HIR::ExprNodeP> vals)
    : ExprNode(mv$(sp))
    , m_vals(mv$(vals)) {
}
ExprNode_ArrayList::ExprNode_ArrayList(Span sp, ::std::vector<::HIR::ExprNodeP> vals)
    : ExprNode(mv$(sp))
    , m_vals(mv$(vals)) {
}
ExprNode_ArraySized::ExprNode_ArraySized(Span sp, ::HIR::ExprNodeP val, ::HIR::ExprPtr size)
    : ExprNode(mv$(sp))
    , m_val(mv$(val))
    , m_size(HIR::ConstGeneric(std::make_unique<HIR::ConstGeneric_Unevaluated>(mv$(size)))) {
}
ExprNode_Closure::ExprNode_Closure(Span sp, args_t args, ::HIR::TypeRef rv, ::HIR::ExprNodeP code, bool is_move)
    : ExprNode(mv$(sp))
    , m_args(::std::move(args))
    , m_return(::std::move(rv))
    , m_code(::std::move(code))
    , m_is_move(is_move) {
}
ExprNode_Generator::ExprNode_Generator(
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
ExprNode_GeneratorWrapper::ExprNode_GeneratorWrapper(
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
ExprNode_AsyncBlock::ExprNode_AsyncBlock(Span sp, ::HIR::ExprNodeP code, bool is_move)
    : ExprNode(mv$(sp))
    , m_code(std::move(code))
    , m_is_move(is_move) {
}
ExprVisitorDef::ExprVisitorDef(TypeInterner& types): m_types(types) {}
}
