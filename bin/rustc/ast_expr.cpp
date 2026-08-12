#include "ast_expr.h"
#include "ast_ast.h"
#include <cctype>

namespace AST {

    ExprNodeP::~ExprNodeP() {
        if (m_ptr) {
            delete m_ptr;
        }
        m_ptr = nullptr;
    }

    ExprNodeP::ExprNodeP(std::unique_ptr<ExprNode> node)
        : m_ptr(node.release())
    {
    }

    const char* ExprNodeP::type_name() const {
        return typeid(*m_ptr).name();
    }

    Expr::Expr(ExprNodeP node)
        : m_node(node.release())
    {
    }

    Expr::Expr(ExprNode* node)
        : m_node(node)
    {
    }

    Expr::Expr()
        : m_node(nullptr)
    {
    }

    void Expr::visit_nodes(NodeVisitor& v) {
        if (m_node) {
            m_node->visit(v);
        }
    }

    void Expr::visit_nodes(NodeVisitor& v) const {
        if (m_node) {
            assert(v.is_const());
            //const_cast<const ExprNode*>(m_node.get())->visit(v);
            m_node->visit(v);
        }
    }

    Expr Expr::clone() const {
        if (m_node) {
            return Expr(m_node->clone());
        } else {
            return Expr();
        }
    }

    ::std::ostream& operator<<(::std::ostream& os, const Expr& pat) {
        if (pat.m_node.get()) {
            return os << *pat.m_node;
        } else {
            return os << "/* null */";
        }
    }

    ::std::ostream& operator<<(::std::ostream& os, const ExprNode& node) {
        assert(static_cast<const void*>(&node) != nullptr);
        node.print(os);
        return os;
    }

    ExprNode::~ExprNode() {
    }

    unsigned int ExprNode_Block::node_kind() const { return ExprNode_Block::kind; }
    unsigned int ExprNode_AsyncBlock::node_kind() const { return ExprNode_AsyncBlock::kind; }
    unsigned int ExprNode_GeneratorBlock::node_kind() const { return ExprNode_GeneratorBlock::kind; }
    unsigned int ExprNode_Try::node_kind() const { return ExprNode_Try::kind; }
    unsigned int ExprNode_Macro::node_kind() const { return ExprNode_Macro::kind; }
    unsigned int ExprNode_Asm::node_kind() const { return ExprNode_Asm::kind; }
    unsigned int ExprNode_Asm2::node_kind() const { return ExprNode_Asm2::kind; }
    unsigned int ExprNode_Flow::node_kind() const { return ExprNode_Flow::kind; }
    unsigned int ExprNode_LetBinding::node_kind() const { return ExprNode_LetBinding::kind; }
    unsigned int ExprNode_Assign::node_kind() const { return ExprNode_Assign::kind; }
    unsigned int ExprNode_CallPath::node_kind() const { return ExprNode_CallPath::kind; }
    unsigned int ExprNode_CallMethod::node_kind() const { return ExprNode_CallMethod::kind; }
    unsigned int ExprNode_CallObject::node_kind() const { return ExprNode_CallObject::kind; }
    unsigned int ExprNode_Loop::node_kind() const { return ExprNode_Loop::kind; }
    unsigned int ExprNode_For::node_kind() const { return ExprNode_For::kind; }
    unsigned int ExprNode_While::node_kind() const { return ExprNode_While::kind; }
    unsigned int ExprNode_Match::node_kind() const { return ExprNode_Match::kind; }
    unsigned int ExprNode_If::node_kind() const { return ExprNode_If::kind; }
    unsigned int ExprNode_WildcardPattern::node_kind() const { return ExprNode_WildcardPattern::kind; }
    unsigned int ExprNode_Integer::node_kind() const { return ExprNode_Integer::kind; }
    unsigned int ExprNode_Float::node_kind() const { return ExprNode_Float::kind; }
    unsigned int ExprNode_Bool::node_kind() const { return ExprNode_Bool::kind; }
    unsigned int ExprNode_String::node_kind() const { return ExprNode_String::kind; }
    unsigned int ExprNode_ByteString::node_kind() const { return ExprNode_ByteString::kind; }
    unsigned int ExprNode_CString::node_kind() const { return ExprNode_CString::kind; }
    unsigned int ExprNode_Closure::node_kind() const { return ExprNode_Closure::kind; }
    unsigned int ExprNode_StructLiteral::node_kind() const { return ExprNode_StructLiteral::kind; }
    unsigned int ExprNode_StructLiteralPattern::node_kind() const { return ExprNode_StructLiteralPattern::kind; }
    unsigned int ExprNode_Array::node_kind() const { return ExprNode_Array::kind; }
    unsigned int ExprNode_Tuple::node_kind() const { return ExprNode_Tuple::kind; }
    unsigned int ExprNode_NamedValue::node_kind() const { return ExprNode_NamedValue::kind; }
    unsigned int ExprNode_Field::node_kind() const { return ExprNode_Field::kind; }
    unsigned int ExprNode_Index::node_kind() const { return ExprNode_Index::kind; }
    unsigned int ExprNode_Deref::node_kind() const { return ExprNode_Deref::kind; }
    unsigned int ExprNode_Cast::node_kind() const { return ExprNode_Cast::kind; }
    unsigned int ExprNode_TypeAnnotation::node_kind() const { return ExprNode_TypeAnnotation::kind; }
    unsigned int ExprNode_BinOp::node_kind() const { return ExprNode_BinOp::kind; }
    unsigned int ExprNode_UniOp::node_kind() const { return ExprNode_UniOp::kind; }
    unsigned int ExprNode_MacroDefinition::node_kind() const { return ExprNode_MacroDefinition::kind; }

#define NODE(class, _print, _clone)       \
    void class ::visit(NodeVisitor& nv) { \
        nv.visit(*this);                  \
    }                                     \
    void class ::print(::std::ostream& os) const _print ExprNodeP class ::clone() const _clone
#define OPT_CLONE(node) (node.get() ? node->clone() : ::AST::ExprNodeP())

    namespace {
        static inline ExprNodeP mk_exprnodep(const Span& pos, AST::ExprNode* en) {
            en->set_span(pos);
            return ExprNodeP(en);
        }

#define NEWNODE(type, ...) mk_exprnodep(span(), new type(__VA_ARGS__))
    }

    NODE(
        ExprNode_Block,
        {
            os << "{";
            for (const auto& n : m_nodes) {
                os << *n.node << (n.has_semicolon ? ";" : "");
            }
            os << "}";
        },
        {
            ::std::vector<Line> nodes;
            for (const auto& n : m_nodes) {
                nodes.push_back({n.has_semicolon, n.node->clone()});
            }
            return NEWNODE(ExprNode_Block, m_block_type, mv$(nodes), m_local_mod);
        }
    )

    NODE(ExprNode_AsyncBlock, { os << "async " << (m_is_move ? "move " : "") << *m_inner; }, { return NEWNODE(ExprNode_AsyncBlock, m_inner->clone(), m_is_move); })
    NODE(ExprNode_GeneratorBlock, { os << "gen " << (m_is_move ? "move " : "") << *m_inner; }, { return NEWNODE(ExprNode_GeneratorBlock, m_inner->clone(), m_is_move); })
    NODE(ExprNode_Try, { os << "try " << *m_inner; }, { return NEWNODE(ExprNode_Try, m_inner->clone()); })

    NODE(
        ExprNode_Macro,
        {
            os << m_path << "!";
            if (m_ident.size() > 0) {
                os << " " << m_ident << " ";
            }
            os << "(" << " /*TODO*/ " << ")";
        },
        { return NEWNODE(ExprNode_Macro, AST::Path(m_path), m_ident, m_tokens.clone(), m_is_braced, m_definition_hygiene); }
    )

    NODE(
        ExprNode_Asm,
        {
            os << "llvm_asm!( \"" << m_text << "\"";
            os << " :";
            for (const auto& v : m_output) {
                os << " \"" << v.name << "\" (" << *v.value << "),";
            }
            os << " :";
            for (const auto& v : m_input) {
                os << " \"" << v.name << "\" (" << *v.value << "),";
            }
            os << " :";
            for (const auto& v : m_clobbers) {
                os << " \"" << v << "\",";
            }
            os << " :";
            for (const auto& v : m_flags) {
                os << " \"" << v << "\",";
            }
            os << " )";
        },
        {
            ::std::vector<ExprNode_Asm::ValRef> outputs;
            for (const auto& v : m_output) {
                outputs.push_back(ExprNode_Asm::ValRef{v.name, v.value->clone()});
            }
            ::std::vector<ExprNode_Asm::ValRef> inputs;
            for (const auto& v : m_input) {
                inputs.push_back(ExprNode_Asm::ValRef{v.name, v.value->clone()});
            }
            return NEWNODE(ExprNode_Asm, m_text, mv$(outputs), mv$(inputs), m_clobbers, m_flags);
        }
    )
}

namespace {
    void print_fmt_string(std::ostream& os, const std::string& s) {
        static const char* hex = "0123456789ABCDEF";
        for (auto c : s) {
            if (c == '{') {
                os << "{{";
            } else if (c == '\\') {
                os << "\\\\";
            } else if (c == '"') {
                os << "\\\"";
            } else if (std::isprint(c)) {
                os << c;
            } else {
                os << "\\x" << hex[c >> 4] << hex[c & 15];
            }
        }
    }
}

void AsmCommon::Line::fmt(std::ostream& os) const {
    os << "\"";
    for (const auto& f : this->frags) {
        print_fmt_string(os, f.before);
        os << "{" << f.index;
        if (f.modifier) {
            os << ":" << f.modifier;
        }
        os << "}";
    }
    print_fmt_string(os, this->trailing);
    os << "\"";
}

namespace AST {
    NODE(
        ExprNode_Asm2,
        {
            os << "asm!( ";
            for (const auto& l : m_lines) {
                l.fmt(os);
                os << ", ";
            }
            for (const auto& p : m_params) {
        TU_MATCH_HDRA( (p), {)
        TU_ARMA(Const, e) {
                    os << "const " << *e;
                }
                TU_ARMA(Sym, e) {
                    os << "sym " << e;
                }
                TU_ARMA(RegSingle, e) {
                    os << "reg(" << e.dir << " " << e.spec << ") " << *e.val;
                }
                TU_ARMA(Reg, e) {
                    os << "reg(" << e.dir << " " << e.spec << ") ";
                    if (e.val_in) {
                        os << *e.val_in;
                    } else {
                        os << "_";
                    }
                    os << " => ";
                    if (e.val_out) {
                        os << *e.val_out;
                    } else {
                        os << "_";
                    }
                }
        }
        os << ", ";
            }
            os << " )";
        },
        {
            std::vector<Param> params;

            for (const auto& p : m_params) {
        TU_MATCH_HDRA( (p), { )
        TU_ARMA(Const, e) {
                    params.push_back(Param::make_Const(e->clone()));
                }
                TU_ARMA(Sym, e) {
                    params.push_back(Param::make_Sym(e));
                }
                TU_ARMA(RegSingle, e) {
                    params.push_back(Param::make_RegSingle({e.dir, e.spec.clone(), e.val->clone()}));
                }
                TU_ARMA(Reg, e) {
                    params.push_back(Param::make_Reg({e.dir, e.spec.clone(), e.val_in ? e.val_in->clone() : nullptr, e.val_out ? e.val_out->clone() : nullptr}));
                }
        }
            }

            return NEWNODE(ExprNode_Asm2, m_options, m_lines, std::move(params));
        }
    )

    NODE(
        ExprNode_Flow,
        {
            switch (m_type) {
                case RETURN:
                    os << "return";
                    break;
                case YIELD:
                    os << "yield";
                    break;
                case BREAK:
                    os << "break";
                    break;
                case CONTINUE:
                    os << "continue";
                    break;
                case YEET:
                    os << "do yeet";
                    break;
            }
            if (m_value) {
                os << " " << *m_value;
            }
        },
        { return NEWNODE(ExprNode_Flow, m_type, m_target, m_value ? m_value->clone() : nullptr); }
    )

    NODE(
        ExprNode_LetBinding,
        {
            os << (m_is_super ? "super let " : "let ") << m_pat << ": " << m_type;
            if (m_value) {
                os << " = " << *m_value;
                if (m_else) {
                    os << " else " << *m_else;
                }
            }
        },
        { return NEWNODE(ExprNode_LetBinding, m_pat.clone(), m_type.clone(), OPT_CLONE(m_value), OPT_CLONE(m_else), m_is_super); }
    )

    NODE(ExprNode_Assign, { os << *m_slot << " = " << *m_value; }, { return NEWNODE(ExprNode_Assign, m_op, m_slot->clone(), m_value->clone()); })

    NODE(
        ExprNode_CallPath,
        {
            os << m_path << "(";
            for (const auto& a : m_args) {
                os << *a << ",";
            }
            os << ")";
        },
        {
            ::std::vector<ExprNodeP> args;
            for (const auto& a : m_args) {
                args.push_back(a->clone());
            }
            return NEWNODE(ExprNode_CallPath, AST::Path(m_path), mv$(args));
        }
    )

    NODE(
        ExprNode_CallMethod,
        {
            os << "(" << *m_val << ")." << m_method << "(";
            for (const auto& a : m_args) {
                os << *a << ",";
            }
            os << ")";
        },
        {
            ::std::vector<ExprNodeP> args;
            for (const auto& a : m_args) {
                args.push_back(a->clone());
            }
            return NEWNODE(ExprNode_CallMethod, m_val->clone(), m_method, mv$(args));
        }
    )

    NODE(
        ExprNode_CallObject,
        {
            os << "(" << *m_val << ")(";
            for (const auto& a : m_args) {
                os << *a << ",";
            }
            os << ")";
        },
        {
            ::std::vector<ExprNodeP> args;
            for (const auto& a : m_args) {
                args.push_back(a->clone());
            }
            return NEWNODE(ExprNode_CallObject, m_val->clone(), mv$(args));
        }
    )

    NODE(ExprNode_Loop, { os << "LOOP [" << m_label << "] " << *m_code; }, { return NEWNODE(ExprNode_Loop, m_label, m_code->clone()); })

    NODE(
        ExprNode_For,
        {
            os << "FOR [" << m_label << "] " << m_pattern << "in" << *m_value;
            os << " " << *m_code;
        },
        { return NEWNODE(ExprNode_For, m_label, m_pattern.clone(), m_value->clone(), m_code->clone()); }
    )

    namespace {
        void fmt_iflet_conditions(::std::ostream& os, const ::std::vector<AST::IfLet_Condition>& conditions) {
            for (const auto& cond : conditions) {
                if (&cond != &conditions.front()) {
                    os << " && ";
                }
                if (cond.opt_pat) {
                    os << "let ";
                    os << *cond.opt_pat << " = ";
                }
                os << "(" << *cond.value << ")";
            }
        }

        ::std::vector<AST::IfLet_Condition> clone_iflet_conditions(const ::std::vector<AST::IfLet_Condition>& conditions) {
            ::std::vector<AST::IfLet_Condition> new_conds;
            new_conds.reserve(conditions.size());
            for (const auto& cond : conditions) {
                AST::IfLet_Condition new_cond;
                if (cond.opt_pat) {
                    new_cond.opt_pat = std::make_unique<AST::Pattern>(cond.opt_pat->clone());
                }
                new_cond.value = cond.value->clone();
                new_conds.push_back(std::move(new_cond));
            }
            return new_conds;
        }
    }

    NODE(
        ExprNode_While,
        {
            if (m_label != "") {
                os << "'" << m_label << ": ";
            }
            os << "while ";
            fmt_iflet_conditions(os, m_conditions);
            os << " { " << *m_code << " }";
        },
        {
            auto new_conds = clone_iflet_conditions(m_conditions);
            return NEWNODE(ExprNode_While, m_label, mv$(new_conds), m_code->clone());
        }
    )

    NODE(
        ExprNode_Match,
        {
            os << "match (" << *m_val << ") {";
            for (const auto& arm : m_arms) {
                for (const auto& pat : arm.m_patterns) {
                    os << " " << pat;
                }
                if (arm.m_guard.size() > 0) {
                    os << " if ";
                    fmt_iflet_conditions(os, arm.m_guard);
                }

                os << " => " << *arm.m_code << ",";
            }
            os << "}";
        },
        {
            ::std::vector<ExprNode_Match_Arm> arms;
            for (const auto& arm : m_arms) {
                ::std::vector<AST::Pattern> patterns;
                for (const auto& pat : arm.m_patterns) {
                    patterns.push_back(pat.clone());
                }
                arms.push_back(ExprNode_Match_Arm(mv$(patterns), clone_iflet_conditions(arm.m_guard), arm.m_code->clone()));
                arms.back().m_attrs = arm.m_attrs.clone();
            }
            return NEWNODE(ExprNode_Match, m_val->clone(), mv$(arms));
        }
    )

    NODE(
        ExprNode_If,
        {
            for (const auto& arm : m_arms) {
                if (&arm != m_arms.data()) {
                    os << " else ";
                }
                os << "if ";
                fmt_iflet_conditions(os, arm.m_conditions);
                os << " { " << *arm.m_body << " }";
            }
            if (m_else) {
                os << " else { " << *m_else << " }";
            }
        },
        {
            std::vector<Arm> arms;
            arms.reserve(m_arms.size());
            for (const auto& arm : m_arms) {
                arms.push_back(ExprNode_If::Arm{clone_iflet_conditions(arm.m_conditions), arm.m_body->clone()});
            }
            return NEWNODE(ExprNode_If, std::move(arms), OPT_CLONE(m_else));
        }
    )

    NODE(ExprNode_WildcardPattern, { os << "_"; }, { return NEWNODE(ExprNode_WildcardPattern); })
    NODE(
        ExprNode_Integer,
        {
            if (m_datatype == CORETYPE_CHAR) {
                os << "'\\u{" << ::std::hex << m_value << ::std::dec << "}'";
            } else {
                os << m_value;
                if (m_datatype == CORETYPE_ANY)
                    ;
                else {
                    os << "_" << coretype_name(m_datatype);
                }
            }
        },
        { return NEWNODE(ExprNode_Integer, m_value, m_datatype); }
    )
    NODE(ExprNode_Float, { os << m_value << "_" << m_datatype; }, { return NEWNODE(ExprNode_Float, m_value, m_datatype); })
    NODE(ExprNode_Bool, { os << m_value; }, { return NEWNODE(ExprNode_Bool, m_value); })
    NODE(ExprNode_String, { os << "\"" << m_value << "\""; }, { return NEWNODE(ExprNode_String, m_value, m_hygiene); })
    NODE(ExprNode_ByteString, { os << "b\"" << m_value << "\""; }, { return NEWNODE(ExprNode_ByteString, m_value); })
    NODE(ExprNode_CString, { os << "c\"" << m_value << "\""; }, { return NEWNODE(ExprNode_CString, m_value); })

    NODE(
        ExprNode_Closure,
        {
            if (m_is_pinned) {
                os << "static ";
            }
            if (m_is_move) {
                os << "move ";
            }
            os << "|";
            for (const auto& a : m_args) {
                os << a.first << ": " << a.second << ",";
            }
            os << "|";
            os << "->" << m_return;
            os << " " << *m_code;
        },
        {
            ExprNode_Closure::args_t args;
            for (const auto& a : m_args) {
                args.push_back(::std::make_pair(a.first.clone(), a.second.clone()));
            }
            return NEWNODE(ExprNode_Closure, mv$(args), m_return.clone(), m_code->clone(), m_is_move, m_is_pinned);
        }
    );

    NODE(
        ExprNode_StructLiteral,
        {
            os << m_path << " { ";
            for (const auto& v : m_values) {
                os << v.name << ": " << *v.value << ", ";
            }
            if (m_base_value) {
                os << ".." << *m_base_value;
            }
            os << "}";
        },
        {
            ExprNode_StructLiteral::t_values vals;

            for (const auto& v : m_values) {
                vals.push_back({v.attrs.clone(), v.name, v.value->clone()});
            }

            return NEWNODE(ExprNode_StructLiteral, AST::Path(m_path), OPT_CLONE(m_base_value), mv$(vals));
        }
    )
    NODE(
        ExprNode_StructLiteralPattern,
        {
            os << m_path << " /*pat*/ { ";
            for (const auto& v : m_values) {
                os << v.name << ": " << *v.value << ", ";
            }
            os << ".. }";
        },
        {
            ExprNode_StructLiteral::t_values vals;

            for (const auto& v : m_values) {
                vals.push_back({v.attrs.clone(), v.name, v.value->clone()});
            }

            return NEWNODE(ExprNode_StructLiteralPattern, AST::Path(m_path), mv$(vals));
        }
    )

    NODE(
        ExprNode_Array,
        {
            os << "[";
            if (m_size.get()) {
                os << *m_values[0] << "; " << *m_size;
            } else {
                for (const auto& a : m_values) {
                    os << *a << ",";
                }
            }
            os << "]";
        },
        {
            if (m_size.get()) {
                return NEWNODE(ExprNode_Array, m_values[0]->clone(), m_size->clone());
            } else {
                ::std::vector<ExprNodeP> nodes;
                for (const auto& n : m_values) {
                    nodes.push_back(n->clone());
                }
                return NEWNODE(ExprNode_Array, mv$(nodes));
            }
        }
    )

    NODE(
        ExprNode_Tuple,
        {
            os << "(";
            for (const auto& a : m_values) {
                os << *a << ",";
            }
            os << ")";
        },
        {
            ::std::vector<ExprNodeP> nodes;
            for (const auto& n : m_values) {
                nodes.push_back(n->clone());
            }
            return NEWNODE(ExprNode_Tuple, mv$(nodes));
        }
    )

    NODE(
        ExprNode_NamedValue,
        {
            m_path.print_pretty(os, false);
            //os << m_path;
        },
        { return NEWNODE(ExprNode_NamedValue, AST::Path(m_path)); }
    )

    NODE(ExprNode_Field, { os << "(" << *m_obj << ")." << m_name; }, { return NEWNODE(ExprNode_Field, m_obj->clone(), m_name); })

    NODE(ExprNode_Index, { os << "(" << *m_obj << ")[" << *m_idx << "]"; }, { return NEWNODE(ExprNode_Index, m_obj->clone(), m_idx->clone()); })

    NODE(ExprNode_Deref, { os << "*(" << *m_value << ")"; }, { return NEWNODE(ExprNode_Deref, m_value->clone()); });

    NODE(ExprNode_Cast, { os << "(" << *m_value << " as " << m_type << ")"; }, { return NEWNODE(ExprNode_Cast, m_value->clone(), m_type.clone()); })
    NODE(ExprNode_TypeAnnotation, { os << "(" << *m_value << ": " << m_type << ")"; }, { return NEWNODE(ExprNode_TypeAnnotation, m_value->clone(), m_type.clone()); })

    NODE(
        ExprNode_BinOp,
        {
            if (m_type == RANGE_INC) {
                os << "(";
                if (m_left) {
                    os << *m_left << " ";
                }
                os << "... " << *m_right;
                os << ")";
                return;
            }
            if (m_type == RANGE) {
                os << "(";
                if (m_left) {
                    os << *m_left;
                }
                os << "..";
                if (m_right) {
                    os << " " << *m_right;
                }
                os << ")";
                return;
            }
            os << "(" << *m_left << " ";
            switch (m_type) {
                case CMPEQU:
                    os << "==";
                    break;
                case CMPNEQU:
                    os << "!=";
                    break;
                case CMPLT:
                    os << "<";
                    break;
                case CMPLTE:
                    os << "<=";
                    break;
                case CMPGT:
                    os << ">";
                    break;
                case CMPGTE:
                    os << ">=";
                    break;
                case BOOLAND:
                    os << "&&";
                    break;
                case BOOLOR:
                    os << "||";
                    break;
                case BITAND:
                    os << "&";
                    break;
                case BITOR:
                    os << "|";
                    break;
                case BITXOR:
                    os << "^";
                    break;
                case SHR:
                    os << ">>";
                    break;
                case SHL:
                    os << "<<";
                    break;
                case MULTIPLY:
                    os << "*";
                    break;
                case DIVIDE:
                    os << "/";
                    break;
                case MODULO:
                    os << "%";
                    break;
                case ADD:
                    os << "+";
                    break;
                case SUB:
                    os << "-";
                    break;
                case RANGE:
                    os << "..";
                    break;
                case RANGE_INC:
                    os << "...";
                    break;
                case PLACE_IN:
                    os << "<-";
                    break;
            }
            os << " " << *m_right << ")";
        },
        { return NEWNODE(ExprNode_BinOp, m_type, OPT_CLONE(m_left), OPT_CLONE(m_right)); }
    )

    NODE(
        ExprNode_UniOp,
        {
            switch (m_type) {
                case NEGATE:
                    os << "(-";
                    break;
                case INVERT:
                    os << "(!";
                    break;
                case BOX:
                    os << "(box ";
                    break;
                case REF:
                    os << "(&";
                    break;
                case REFMUT:
                    os << "(&mut ";
                    break;
                case RawBorrow:
                    os << "(&raw const ";
                    break;
                case RawBorrowMut:
                    os << "(&raw mut ";
                    break;
                case QMARK:
                    os << "(" << *m_value << "?)";
                    return;
                case AWait:
                    os << "((" << *m_value << ").await)";
                    return;
            }
            os << *m_value << ")";
        },
        { return NEWNODE(ExprNode_UniOp, m_type, m_value->clone()); }
    )

    NODE(
        ExprNode_MacroDefinition,
        { os << "/* macro definition #" << m_definition_id << " */"; },
        { return NEWNODE(ExprNode_MacroDefinition, m_definition_id, m_token_hygiene, m_definition_hygiene); }
    )

#define NV(type, actions)                                              \
    void NodeVisitorDef::visit(type& node) { /*DEBUG("DEF - "#type);*/ \
        actions                                                        \
    }
    //  void NodeVisitorDef::visit(const type& node) { DEBUG("DEF - "#type" (const)"); actions }

    NV(ExprNode_Block, {
        //INDENT();
        for (auto& child : node.m_nodes) {
            visit(child.node);
        }
        //UNINDENT();
    })
    NV(ExprNode_AsyncBlock, { visit(node.m_inner); })
    NV(ExprNode_GeneratorBlock, { visit(node.m_inner); })
    NV(ExprNode_Try, { visit(node.m_inner); })
    NV(ExprNode_Macro, { BUG(node.span(), "Hit unexpanded macro in expression - " << node); })
    NV(ExprNode_Asm, {
        for (auto& v : node.m_output) {
            visit(v.value);
        }
        for (auto& v : node.m_input) {
            visit(v.value);
        }
    })
    NV(ExprNode_Asm2, {
        for (auto& v : node.m_params) {
        TU_MATCH_HDRA((v), {)
        TU_ARMA(Const, e) {
                    visit(e);
                }
                TU_ARMA(Sym, e) {
                    //visit(e);
                }
                TU_ARMA(RegSingle, e) {
                    visit(e.val);
                }
                TU_ARMA(Reg, e) {
                    visit(e.val_in);
                    visit(e.val_out);
                }
        }
        }
    })
    NV(ExprNode_Flow, { visit(node.m_value); })
    NV(ExprNode_LetBinding, {
        // TODO: Handle recurse into Let pattern?
        visit(node.m_value);
        visit(node.m_else);
    })
    NV(ExprNode_Assign, {
        INDENT();
        visit(node.m_slot);
        visit(node.m_value);
        UNINDENT();
    })
    NV(ExprNode_CallPath, {
        INDENT();
        for (auto& arg : node.m_args) {
            visit(arg);
        }
        UNINDENT();
    })
    NV(ExprNode_CallMethod, {
        INDENT();
        visit(node.m_val);
        for (auto& arg : node.m_args) {
            visit(arg);
        }
        UNINDENT();
    })
    NV(ExprNode_CallObject, {
        INDENT();
        visit(node.m_val);
        for (auto& arg : node.m_args) {
            visit(arg);
        }
        UNINDENT();
    })
    NV(ExprNode_Loop, {
        INDENT();
        visit(node.m_code);
        UNINDENT();
    })
    NV(ExprNode_For, {
        INDENT();
        visit(node.m_value);
        visit(node.m_code);
        UNINDENT();
    })
    NV(ExprNode_While, {
        INDENT();
        for (auto& c : node.m_conditions) {
            visit(c.value);
        }
        visit(node.m_code);
        UNINDENT();
    })
    NV(ExprNode_Match, {
        INDENT();
        visit(node.m_val);
        for (auto& arm : node.m_arms) {
            for (auto& c : arm.m_guard) {
                visit(c.value);
            }
            visit(arm.m_code);
        }
        UNINDENT();
    })
    NV(ExprNode_If, {
        INDENT();
        for (auto& a : node.m_arms) {
            for (auto& c : a.m_conditions) {
                visit(c.value);
            }
            visit(a.m_body);
        }
        visit(node.m_else);
        UNINDENT();
    })

    NV(ExprNode_WildcardPattern, { (void)node; })
    NV(ExprNode_Integer, { (void)node; })
    NV(ExprNode_Float, { (void)node; })
    NV(ExprNode_Bool, { (void)node; })
    NV(ExprNode_String, { (void)node; })
    NV(ExprNode_ByteString, { (void)node; })
    NV(ExprNode_CString, { (void)node; })

    NV(ExprNode_Closure, { visit(node.m_code); });
    NV(ExprNode_StructLiteral, {
        visit(node.m_base_value);
        for (auto& val : node.m_values) {
            visit(val.value);
        }
    })
    NV(ExprNode_StructLiteralPattern, {
        for (auto& val : node.m_values) {
            visit(val.value);
        }
    })
    NV(ExprNode_Array, {
        visit(node.m_size);
        for (auto& val : node.m_values) {
            visit(val);
        }
    })
    NV(ExprNode_Tuple, {
        for (auto& val : node.m_values) {
            visit(val);
        }
    })
    NV(ExprNode_NamedValue, {
        (void)node;
        // LEAF
    })

    NV(ExprNode_Field, { visit(node.m_obj); })
    NV(ExprNode_Index, {
        visit(node.m_obj);
        visit(node.m_idx);
    })
    NV(ExprNode_Deref, { visit(node.m_value); })
    NV(ExprNode_Cast, { visit(node.m_value); })
    NV(ExprNode_TypeAnnotation, { visit(node.m_value); })
    NV(ExprNode_BinOp, {
        visit(node.m_left);
        visit(node.m_right);
    })
    NV(ExprNode_UniOp, { visit(node.m_value); })
    NV(ExprNode_MacroDefinition, {})
#undef NV

};

namespace AST {

void ExprNode::set_attrs(AttributeList&& mi) {
    for (auto& i : mi.m_items) {
        m_attrs.m_items.push_back(mv$(i));
    }
    mi.m_items.clear();
}
ExprNode_Block::ExprNode_Block(::std::vector<Line> nodes)
    : m_block_type(Type::Bare)
    , m_label("")
    , m_local_mod()
    , m_nodes(::std::move(nodes)) {
}
/// Shortcut for a block that returns a contained node
ExprNode_Block::ExprNode_Block(ExprNodeP value)
    : ExprNode_Block() {
    set_span(value->span());
    m_nodes.push_back({false, std::move(value)});
}
ExprNode_Block::ExprNode_Block(Type type, ::std::vector<Line> nodes, ::std::shared_ptr<AST::Module> local_mod)
    : m_block_type(type)
    , m_label("")
    , m_local_mod(::std::move(local_mod))
    , m_nodes(::std::move(nodes)) {
}
ExprNode_AsyncBlock::ExprNode_AsyncBlock(ExprNodeP inner, bool is_move)
    : m_inner(std::move(inner))
    , m_is_move(is_move) {
}
ExprNode_GeneratorBlock::ExprNode_GeneratorBlock(ExprNodeP inner, bool is_move)
    : m_inner(std::move(inner))
    , m_is_move(is_move) {
}
ExprNode_Try::ExprNode_Try(ExprNodeP inner)
    : m_inner(::std::move(inner)) {
}
ExprNode_Macro::ExprNode_Macro(AST::Path name, RcString ident, ::TokenTree&& tokens, bool is_braced, Ident::Hygiene definition_hygiene)
    : m_path(::std::move(name))
    , m_ident(ident)
    , m_tokens(::std::move(tokens))
    , m_is_braced(is_braced)
    , m_definition_hygiene(::std::move(definition_hygiene)) {
}
ExprNode_Asm::ExprNode_Asm(::std::string text, ::std::vector<ValRef> output, ::std::vector<ValRef> input, ::std::vector<::std::string> clobbers, ::std::vector<::std::string> flags)
    : m_text(::std::move(text))
    , m_output(::std::move(output))
    , m_input(::std::move(input))
    , m_clobbers(::std::move(clobbers))
    , m_flags(::std::move(flags)) {
}
ExprNode_Asm2::ExprNode_Asm2(AsmCommon::Options options, std::vector<AsmCommon::Line> lines, std::vector<Param> params)
    : m_options(options)
    , m_lines(::std::move(lines))
    , m_params(::std::move(params)) {
}
ExprNode_Flow::ExprNode_Flow(Type type, Ident target, ExprNodeP value)
    : m_type(type)
    , m_target(::std::move(target))
    , m_value(::std::move(value)) {
}
ExprNode_LetBinding::ExprNode_LetBinding(Pattern pat, TypeRef type, ExprNodeP value, ExprNodeP else_arm, bool is_super)
    : m_pat(::std::move(pat))
    , m_type(::std::move(type))
    , m_value(::std::move(value))
    , m_else(::std::move(else_arm))
    , m_is_super(is_super) {
}
ExprNode_Assign::ExprNode_Assign()
    : m_op(NONE) {
}
ExprNode_Assign::ExprNode_Assign(Operation op, ExprNodeP slot, ExprNodeP value)
    : m_op(op)
    , m_slot(::std::move(slot))
    , m_value(::std::move(value)) {
}
ExprNode_CallPath::ExprNode_CallPath(Path&& path, ::std::vector<ExprNodeP>&& args)
    : m_path(::std::move(path))
    , m_args(::std::move(args)) {
}
ExprNode_CallMethod::ExprNode_CallMethod(ExprNodeP obj, PathNode method, ::std::vector<ExprNodeP> args)
    : m_val(::std::move(obj))
    , m_method(::std::move(method))
    , m_args(::std::move(args)) {
}
ExprNode_CallObject::ExprNode_CallObject(ExprNodeP val, ::std::vector<ExprNodeP>&& args)
    : m_val(::std::move(val))
    , m_args(::std::move(args)) {
}
ExprNode_Loop::ExprNode_Loop()
    : m_label("") {
}
ExprNode_Loop::ExprNode_Loop(Ident label, ExprNodeP code)
    : m_label(::std::move(label))
    , m_code(::std::move(code)) {
}
ExprNode_For::ExprNode_For(Ident label, AST::Pattern pattern, ExprNodeP val, ExprNodeP code)
    : m_label(::std::move(label))
    , m_pattern(::std::move(pattern))
    , m_value(::std::move(val))
    , m_code(::std::move(code)) {
}
ExprNode_While::ExprNode_While(Ident label, std::vector<IfLet_Condition> conditions, ExprNodeP code)
    : m_label(::std::move(label))
    , m_conditions(::std::move(conditions))
    , m_code(::std::move(code)) {
}
ExprNode_Match_Arm::ExprNode_Match_Arm() {
}
ExprNode_Match_Arm::ExprNode_Match_Arm(::std::vector<Pattern> patterns, std::vector<IfLet_Condition> guard, ExprNodeP code)
    : m_patterns(mv$(patterns))
    , m_guard(mv$(guard))
    , m_code(mv$(code)) {
}
ExprNode_Match::ExprNode_Match(ExprNodeP val, ::std::vector<ExprNode_Match_Arm> arms)
    : m_val(::std::move(val))
    , m_arms(::std::move(arms)) {
}
ExprNode_If::ExprNode_If(std::vector<Arm> arms, ExprNodeP else_code)
    : m_arms(::std::move(arms))
    , m_else(::std::move(else_code)) {
}
ExprNode_Integer::ExprNode_Integer(U128 value, enum eCoreType datatype)
    : m_datatype(datatype)
    , m_value(value) {
}
ExprNode_Float::ExprNode_Float(FloatValue value, enum eCoreType datatype)
    : m_datatype(datatype)
    , m_value(value) {
}
ExprNode_Bool::ExprNode_Bool(bool value)
    : m_value(value) {
}
ExprNode_String::ExprNode_String(::std::string value, Ident::Hygiene h)
    : m_value(::std::move(value))
    , m_hygiene(::std::move(h)) {
}
ExprNode_ByteString::ExprNode_ByteString(::std::string value)
    : m_value(::std::move(value)) {
}
ExprNode_CString::ExprNode_CString(::std::string value)
    : m_value(::std::move(value)) {
}
ExprNode_StructLiteral::ExprNode_StructLiteral(Path path, ExprNodeP base_value, t_values&& values)
    : m_path(std::move(path))
    , m_base_value(std::move(base_value))
    , m_values(std::move(values)) {
}
ExprNode_StructLiteralPattern::ExprNode_StructLiteralPattern(Path path, t_values&& values)
    : m_path(std::move(path))
    , m_values(std::move(values)) {
}
ExprNode_Array::ExprNode_Array(::std::vector<ExprNodeP> vals)
    : m_values(::std::move(vals)) {
}
ExprNode_Array::ExprNode_Array(ExprNodeP val, ExprNodeP size)
    : m_size(::std::move(size)) {
    m_values.push_back(::std::move(val));
}
ExprNode_Tuple::ExprNode_Tuple(::std::vector<ExprNodeP> vals)
    : m_values(::std::move(vals)) {
}
ExprNode_NamedValue::ExprNode_NamedValue(Path path)
    : m_path(::std::move(path)) {
}
ExprNode_Field::ExprNode_Field(ExprNodeP obj, RcString name)
    : m_obj(::std::move(obj))
    , m_name(::std::move(name)) {
}
ExprNode_Index::ExprNode_Index(ExprNodeP obj, ExprNodeP idx)
    : m_obj(::std::move(obj))
    , m_idx(::std::move(idx)) {
}
ExprNode_Deref::ExprNode_Deref(ExprNodeP value)
    : m_value(::std::move(value)) {
}
ExprNode_Cast::ExprNode_Cast(ExprNodeP value, TypeRef&& dst_type)
    : m_value(::std::move(value))
    , m_type(::std::move(dst_type)) {
}
ExprNode_TypeAnnotation::ExprNode_TypeAnnotation(ExprNodeP value, TypeRef&& dst_type)
    : m_value(::std::move(value))
    , m_type(::std::move(dst_type)) {
}
ExprNode_BinOp::ExprNode_BinOp(Type type, ExprNodeP left, ExprNodeP right)
    : m_type(type)
    , m_left(::std::move(left))
    , m_right(::std::move(right)) {
}
ExprNode_UniOp::ExprNode_UniOp(Type type, ExprNodeP value)
    : m_type(type)
    , m_value(::std::move(value)) {
}
ExprNode_MacroDefinition::ExprNode_MacroDefinition(unsigned int definition_id, Ident::Hygiene token_hygiene, Ident::Hygiene definition_hygiene)
    : m_definition_id(definition_id)
    , m_token_hygiene(::std::move(token_hygiene))
    , m_definition_hygiene(::std::move(definition_hygiene)) {
}
void NodeVisitor::visit(ExprNodeP& cnode) {
    if (cnode.get()) {
        cnode->visit(*this);
    }
}
void NodeVisitorDef::visit(ExprNodeP& cnode) {
    if (cnode.is_valid()) {
        TRACE_FUNCTION_F(cnode.type_name());
        cnode->visit(*this);
    }
}
}
