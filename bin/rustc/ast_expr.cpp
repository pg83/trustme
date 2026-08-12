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

    unsigned int ExprNodeBlock::node_kind() const { return ExprNodeBlock::kind; }
    unsigned int ExprNodeAsyncBlock::node_kind() const { return ExprNodeAsyncBlock::kind; }
    unsigned int ExprNodeGeneratorBlock::node_kind() const { return ExprNodeGeneratorBlock::kind; }
    unsigned int ExprNodeTry::node_kind() const { return ExprNodeTry::kind; }
    unsigned int ExprNodeMacro::node_kind() const { return ExprNodeMacro::kind; }
    unsigned int ExprNodeAsm::node_kind() const { return ExprNodeAsm::kind; }
    unsigned int ExprNodeAsm2::node_kind() const { return ExprNodeAsm2::kind; }
    unsigned int ExprNodeFlow::node_kind() const { return ExprNodeFlow::kind; }
    unsigned int ExprNodeLetBinding::node_kind() const { return ExprNodeLetBinding::kind; }
    unsigned int ExprNodeAssign::node_kind() const { return ExprNodeAssign::kind; }
    unsigned int ExprNodeCallPath::node_kind() const { return ExprNodeCallPath::kind; }
    unsigned int ExprNodeCallMethod::node_kind() const { return ExprNodeCallMethod::kind; }
    unsigned int ExprNodeCallObject::node_kind() const { return ExprNodeCallObject::kind; }
    unsigned int ExprNodeLoop::node_kind() const { return ExprNodeLoop::kind; }
    unsigned int ExprNodeFor::node_kind() const { return ExprNodeFor::kind; }
    unsigned int ExprNodeWhile::node_kind() const { return ExprNodeWhile::kind; }
    unsigned int ExprNodeMatch::node_kind() const { return ExprNodeMatch::kind; }
    unsigned int ExprNodeIf::node_kind() const { return ExprNodeIf::kind; }
    unsigned int ExprNodeWildcardPattern::node_kind() const { return ExprNodeWildcardPattern::kind; }
    unsigned int ExprNodeInteger::node_kind() const { return ExprNodeInteger::kind; }
    unsigned int ExprNodeFloat::node_kind() const { return ExprNodeFloat::kind; }
    unsigned int ExprNodeBool::node_kind() const { return ExprNodeBool::kind; }
    unsigned int ExprNodeString::node_kind() const { return ExprNodeString::kind; }
    unsigned int ExprNodeByteString::node_kind() const { return ExprNodeByteString::kind; }
    unsigned int ExprNodeCString::node_kind() const { return ExprNodeCString::kind; }
    unsigned int ExprNodeClosure::node_kind() const { return ExprNodeClosure::kind; }
    unsigned int ExprNodeStructLiteral::node_kind() const { return ExprNodeStructLiteral::kind; }
    unsigned int ExprNodeStructLiteralPattern::node_kind() const { return ExprNodeStructLiteralPattern::kind; }
    unsigned int ExprNodeArray::node_kind() const { return ExprNodeArray::kind; }
    unsigned int ExprNodeTuple::node_kind() const { return ExprNodeTuple::kind; }
    unsigned int ExprNodeNamedValue::node_kind() const { return ExprNodeNamedValue::kind; }
    unsigned int ExprNodeField::node_kind() const { return ExprNodeField::kind; }
    unsigned int ExprNodeIndex::node_kind() const { return ExprNodeIndex::kind; }
    unsigned int ExprNodeDeref::node_kind() const { return ExprNodeDeref::kind; }
    unsigned int ExprNodeCast::node_kind() const { return ExprNodeCast::kind; }
    unsigned int ExprNodeTypeAnnotation::node_kind() const { return ExprNodeTypeAnnotation::kind; }
    unsigned int ExprNodeBinOp::node_kind() const { return ExprNodeBinOp::kind; }
    unsigned int ExprNodeUniOp::node_kind() const { return ExprNodeUniOp::kind; }
    unsigned int ExprNodeMacroDefinition::node_kind() const { return ExprNodeMacroDefinition::kind; }

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
        ExprNodeBlock,
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
            return NEWNODE(ExprNodeBlock, m_block_type, mv$(nodes), m_local_mod);
        }
    )

    NODE(ExprNodeAsyncBlock, { os << "async " << (m_is_move ? "move " : "") << *m_inner; }, { return NEWNODE(ExprNodeAsyncBlock, m_inner->clone(), m_is_move); })
    NODE(ExprNodeGeneratorBlock, { os << "gen " << (m_is_move ? "move " : "") << *m_inner; }, { return NEWNODE(ExprNodeGeneratorBlock, m_inner->clone(), m_is_move); })
    NODE(ExprNodeTry, { os << "try " << *m_inner; }, { return NEWNODE(ExprNodeTry, m_inner->clone()); })

    NODE(
        ExprNodeMacro,
        {
            os << m_path << "!";
            if (m_ident.size() > 0) {
                os << " " << m_ident << " ";
            }
            os << "(" << " /*TODO*/ " << ")";
        },
        { return NEWNODE(ExprNodeMacro, AST::Path(m_path), m_ident, m_tokens.clone(), m_is_braced, m_definition_hygiene); }
    )

    NODE(
        ExprNodeAsm,
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
            ::std::vector<ExprNodeAsm::ValRef> outputs;
            for (const auto& v : m_output) {
                outputs.push_back(ExprNodeAsm::ValRef{v.name, v.value->clone()});
            }
            ::std::vector<ExprNodeAsm::ValRef> inputs;
            for (const auto& v : m_input) {
                inputs.push_back(ExprNodeAsm::ValRef{v.name, v.value->clone()});
            }
            return NEWNODE(ExprNodeAsm, m_text, mv$(outputs), mv$(inputs), m_clobbers, m_flags);
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
        ExprNodeAsm2,
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

            return NEWNODE(ExprNodeAsm2, m_options, m_lines, std::move(params));
        }
    )

    NODE(
        ExprNodeFlow,
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
        { return NEWNODE(ExprNodeFlow, m_type, m_target, m_value ? m_value->clone() : nullptr); }
    )

    NODE(
        ExprNodeLetBinding,
        {
            os << (m_is_super ? "super let " : "let ") << m_pat << ": " << m_type;
            if (m_value) {
                os << " = " << *m_value;
                if (m_else) {
                    os << " else " << *m_else;
                }
            }
        },
        { return NEWNODE(ExprNodeLetBinding, m_pat.clone(), m_type.clone(), OPT_CLONE(m_value), OPT_CLONE(m_else), m_is_super); }
    )

    NODE(ExprNodeAssign, { os << *m_slot << " = " << *m_value; }, { return NEWNODE(ExprNodeAssign, m_op, m_slot->clone(), m_value->clone()); })

    NODE(
        ExprNodeCallPath,
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
            return NEWNODE(ExprNodeCallPath, AST::Path(m_path), mv$(args));
        }
    )

    NODE(
        ExprNodeCallMethod,
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
            return NEWNODE(ExprNodeCallMethod, m_val->clone(), m_method, mv$(args));
        }
    )

    NODE(
        ExprNodeCallObject,
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
            return NEWNODE(ExprNodeCallObject, m_val->clone(), mv$(args));
        }
    )

    NODE(ExprNodeLoop, { os << "LOOP [" << m_label << "] " << *m_code; }, { return NEWNODE(ExprNodeLoop, m_label, m_code->clone()); })

    NODE(
        ExprNodeFor,
        {
            os << "FOR [" << m_label << "] " << m_pattern << "in" << *m_value;
            os << " " << *m_code;
        },
        { return NEWNODE(ExprNodeFor, m_label, m_pattern.clone(), m_value->clone(), m_code->clone()); }
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
        ExprNodeWhile,
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
            return NEWNODE(ExprNodeWhile, m_label, mv$(new_conds), m_code->clone());
        }
    )

    NODE(
        ExprNodeMatch,
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
            ::std::vector<ExprNodeMatchArm> arms;
            for (const auto& arm : m_arms) {
                ::std::vector<AST::Pattern> patterns;
                for (const auto& pat : arm.m_patterns) {
                    patterns.push_back(pat.clone());
                }
                arms.push_back(ExprNodeMatchArm(mv$(patterns), clone_iflet_conditions(arm.m_guard), arm.m_code->clone()));
                arms.back().m_attrs = arm.m_attrs.clone();
            }
            return NEWNODE(ExprNodeMatch, m_val->clone(), mv$(arms));
        }
    )

    NODE(
        ExprNodeIf,
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
                arms.push_back(ExprNodeIf::Arm{clone_iflet_conditions(arm.m_conditions), arm.m_body->clone()});
            }
            return NEWNODE(ExprNodeIf, std::move(arms), OPT_CLONE(m_else));
        }
    )

    NODE(ExprNodeWildcardPattern, { os << "_"; }, { return NEWNODE(ExprNodeWildcardPattern); })
    NODE(
        ExprNodeInteger,
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
        { return NEWNODE(ExprNodeInteger, m_value, m_datatype); }
    )
    NODE(ExprNodeFloat, { os << m_value << "_" << m_datatype; }, { return NEWNODE(ExprNodeFloat, m_value, m_datatype); })
    NODE(ExprNodeBool, { os << m_value; }, { return NEWNODE(ExprNodeBool, m_value); })
    NODE(ExprNodeString, { os << "\"" << m_value << "\""; }, { return NEWNODE(ExprNodeString, m_value, m_hygiene); })
    NODE(ExprNodeByteString, { os << "b\"" << m_value << "\""; }, { return NEWNODE(ExprNodeByteString, m_value); })
    NODE(ExprNodeCString, { os << "c\"" << m_value << "\""; }, { return NEWNODE(ExprNodeCString, m_value); })

    NODE(
        ExprNodeClosure,
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
            ExprNodeClosure::args_t args;
            for (const auto& a : m_args) {
                args.push_back(::std::make_pair(a.first.clone(), a.second.clone()));
            }
            return NEWNODE(ExprNodeClosure, mv$(args), m_return.clone(), m_code->clone(), m_is_move, m_is_pinned);
        }
    );

    NODE(
        ExprNodeStructLiteral,
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
            ExprNodeStructLiteral::t_values vals;

            for (const auto& v : m_values) {
                vals.push_back({v.attrs.clone(), v.name, v.value->clone()});
            }

            return NEWNODE(ExprNodeStructLiteral, AST::Path(m_path), OPT_CLONE(m_base_value), mv$(vals));
        }
    )
    NODE(
        ExprNodeStructLiteralPattern,
        {
            os << m_path << " /*pat*/ { ";
            for (const auto& v : m_values) {
                os << v.name << ": " << *v.value << ", ";
            }
            os << ".. }";
        },
        {
            ExprNodeStructLiteral::t_values vals;

            for (const auto& v : m_values) {
                vals.push_back({v.attrs.clone(), v.name, v.value->clone()});
            }

            return NEWNODE(ExprNodeStructLiteralPattern, AST::Path(m_path), mv$(vals));
        }
    )

    NODE(
        ExprNodeArray,
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
                return NEWNODE(ExprNodeArray, m_values[0]->clone(), m_size->clone());
            } else {
                ::std::vector<ExprNodeP> nodes;
                for (const auto& n : m_values) {
                    nodes.push_back(n->clone());
                }
                return NEWNODE(ExprNodeArray, mv$(nodes));
            }
        }
    )

    NODE(
        ExprNodeTuple,
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
            return NEWNODE(ExprNodeTuple, mv$(nodes));
        }
    )

    NODE(
        ExprNodeNamedValue,
        {
            m_path.print_pretty(os, false);
            //os << m_path;
        },
        { return NEWNODE(ExprNodeNamedValue, AST::Path(m_path)); }
    )

    NODE(ExprNodeField, { os << "(" << *m_obj << ")." << m_name; }, { return NEWNODE(ExprNodeField, m_obj->clone(), m_name); })

    NODE(ExprNodeIndex, { os << "(" << *m_obj << ")[" << *m_idx << "]"; }, { return NEWNODE(ExprNodeIndex, m_obj->clone(), m_idx->clone()); })

    NODE(ExprNodeDeref, { os << "*(" << *m_value << ")"; }, { return NEWNODE(ExprNodeDeref, m_value->clone()); });

    NODE(ExprNodeCast, { os << "(" << *m_value << " as " << m_type << ")"; }, { return NEWNODE(ExprNodeCast, m_value->clone(), m_type.clone()); })
    NODE(ExprNodeTypeAnnotation, { os << "(" << *m_value << ": " << m_type << ")"; }, { return NEWNODE(ExprNodeTypeAnnotation, m_value->clone(), m_type.clone()); })

    NODE(
        ExprNodeBinOp,
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
        { return NEWNODE(ExprNodeBinOp, m_type, OPT_CLONE(m_left), OPT_CLONE(m_right)); }
    )

    NODE(
        ExprNodeUniOp,
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
        { return NEWNODE(ExprNodeUniOp, m_type, m_value->clone()); }
    )

    NODE(
        ExprNodeMacroDefinition,
        { os << "/* macro definition #" << m_definition_id << " */"; },
        { return NEWNODE(ExprNodeMacroDefinition, m_definition_id, m_token_hygiene, m_definition_hygiene); }
    )

#define NV(type, actions)                                              \
    void NodeVisitorDef::visit(type& node) { /*DEBUG("DEF - "#type);*/ \
        actions                                                        \
    }
    //  void NodeVisitorDef::visit(const type& node) { DEBUG("DEF - "#type" (const)"); actions }

    NV(ExprNodeBlock, {
        //INDENT();
        for (auto& child : node.m_nodes) {
            visit(child.node);
        }
        //UNINDENT();
    })
    NV(ExprNodeAsyncBlock, { visit(node.m_inner); })
    NV(ExprNodeGeneratorBlock, { visit(node.m_inner); })
    NV(ExprNodeTry, { visit(node.m_inner); })
    NV(ExprNodeMacro, { BUG(node.span(), "Hit unexpanded macro in expression - " << node); })
    NV(ExprNodeAsm, {
        for (auto& v : node.m_output) {
            visit(v.value);
        }
        for (auto& v : node.m_input) {
            visit(v.value);
        }
    })
    NV(ExprNodeAsm2, {
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
    NV(ExprNodeFlow, { visit(node.m_value); })
    NV(ExprNodeLetBinding, {
        // TODO: Handle recurse into Let pattern?
        visit(node.m_value);
        visit(node.m_else);
    })
    NV(ExprNodeAssign, {
        INDENT();
        visit(node.m_slot);
        visit(node.m_value);
        UNINDENT();
    })
    NV(ExprNodeCallPath, {
        INDENT();
        for (auto& arg : node.m_args) {
            visit(arg);
        }
        UNINDENT();
    })
    NV(ExprNodeCallMethod, {
        INDENT();
        visit(node.m_val);
        for (auto& arg : node.m_args) {
            visit(arg);
        }
        UNINDENT();
    })
    NV(ExprNodeCallObject, {
        INDENT();
        visit(node.m_val);
        for (auto& arg : node.m_args) {
            visit(arg);
        }
        UNINDENT();
    })
    NV(ExprNodeLoop, {
        INDENT();
        visit(node.m_code);
        UNINDENT();
    })
    NV(ExprNodeFor, {
        INDENT();
        visit(node.m_value);
        visit(node.m_code);
        UNINDENT();
    })
    NV(ExprNodeWhile, {
        INDENT();
        for (auto& c : node.m_conditions) {
            visit(c.value);
        }
        visit(node.m_code);
        UNINDENT();
    })
    NV(ExprNodeMatch, {
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
    NV(ExprNodeIf, {
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

    NV(ExprNodeWildcardPattern, { (void)node; })
    NV(ExprNodeInteger, { (void)node; })
    NV(ExprNodeFloat, { (void)node; })
    NV(ExprNodeBool, { (void)node; })
    NV(ExprNodeString, { (void)node; })
    NV(ExprNodeByteString, { (void)node; })
    NV(ExprNodeCString, { (void)node; })

    NV(ExprNodeClosure, { visit(node.m_code); });
    NV(ExprNodeStructLiteral, {
        visit(node.m_base_value);
        for (auto& val : node.m_values) {
            visit(val.value);
        }
    })
    NV(ExprNodeStructLiteralPattern, {
        for (auto& val : node.m_values) {
            visit(val.value);
        }
    })
    NV(ExprNodeArray, {
        visit(node.m_size);
        for (auto& val : node.m_values) {
            visit(val);
        }
    })
    NV(ExprNodeTuple, {
        for (auto& val : node.m_values) {
            visit(val);
        }
    })
    NV(ExprNodeNamedValue, {
        (void)node;
        // LEAF
    })

    NV(ExprNodeField, { visit(node.m_obj); })
    NV(ExprNodeIndex, {
        visit(node.m_obj);
        visit(node.m_idx);
    })
    NV(ExprNodeDeref, { visit(node.m_value); })
    NV(ExprNodeCast, { visit(node.m_value); })
    NV(ExprNodeTypeAnnotation, { visit(node.m_value); })
    NV(ExprNodeBinOp, {
        visit(node.m_left);
        visit(node.m_right);
    })
    NV(ExprNodeUniOp, { visit(node.m_value); })
    NV(ExprNodeMacroDefinition, {})
#undef NV

};

namespace AST {

void ExprNode::set_attrs(AttributeList&& mi) {
    for (auto& i : mi.m_items) {
        m_attrs.m_items.push_back(mv$(i));
    }
    mi.m_items.clear();
}
ExprNodeBlock::ExprNodeBlock(::std::vector<Line> nodes)
    : m_block_type(Type::Bare)
    , m_label("")
    , m_local_mod()
    , m_nodes(::std::move(nodes)) {
}
/// Shortcut for a block that returns a contained node
ExprNodeBlock::ExprNodeBlock(ExprNodeP value)
    : ExprNodeBlock() {
    set_span(value->span());
    m_nodes.push_back({false, std::move(value)});
}
ExprNodeBlock::ExprNodeBlock(Type type, ::std::vector<Line> nodes, ::std::shared_ptr<AST::Module> local_mod)
    : m_block_type(type)
    , m_label("")
    , m_local_mod(::std::move(local_mod))
    , m_nodes(::std::move(nodes)) {
}
ExprNodeAsyncBlock::ExprNodeAsyncBlock(ExprNodeP inner, bool is_move)
    : m_inner(std::move(inner))
    , m_is_move(is_move) {
}
ExprNodeGeneratorBlock::ExprNodeGeneratorBlock(ExprNodeP inner, bool is_move)
    : m_inner(std::move(inner))
    , m_is_move(is_move) {
}
ExprNodeTry::ExprNodeTry(ExprNodeP inner)
    : m_inner(::std::move(inner)) {
}
ExprNodeMacro::ExprNodeMacro(AST::Path name, RcString ident, ::TokenTree&& tokens, bool is_braced, Ident::Hygiene definition_hygiene)
    : m_path(::std::move(name))
    , m_ident(ident)
    , m_tokens(::std::move(tokens))
    , m_is_braced(is_braced)
    , m_definition_hygiene(::std::move(definition_hygiene)) {
}
ExprNodeAsm::ExprNodeAsm(::std::string text, ::std::vector<ValRef> output, ::std::vector<ValRef> input, ::std::vector<::std::string> clobbers, ::std::vector<::std::string> flags)
    : m_text(::std::move(text))
    , m_output(::std::move(output))
    , m_input(::std::move(input))
    , m_clobbers(::std::move(clobbers))
    , m_flags(::std::move(flags)) {
}
ExprNodeAsm2::ExprNodeAsm2(AsmCommon::Options options, std::vector<AsmCommon::Line> lines, std::vector<Param> params)
    : m_options(options)
    , m_lines(::std::move(lines))
    , m_params(::std::move(params)) {
}
ExprNodeFlow::ExprNodeFlow(Type type, Ident target, ExprNodeP value)
    : m_type(type)
    , m_target(::std::move(target))
    , m_value(::std::move(value)) {
}
ExprNodeLetBinding::ExprNodeLetBinding(Pattern pat, TypeRef type, ExprNodeP value, ExprNodeP else_arm, bool is_super)
    : m_pat(::std::move(pat))
    , m_type(::std::move(type))
    , m_value(::std::move(value))
    , m_else(::std::move(else_arm))
    , m_is_super(is_super) {
}
ExprNodeAssign::ExprNodeAssign()
    : m_op(NONE) {
}
ExprNodeAssign::ExprNodeAssign(Operation op, ExprNodeP slot, ExprNodeP value)
    : m_op(op)
    , m_slot(::std::move(slot))
    , m_value(::std::move(value)) {
}
ExprNodeCallPath::ExprNodeCallPath(Path&& path, ::std::vector<ExprNodeP>&& args)
    : m_path(::std::move(path))
    , m_args(::std::move(args)) {
}
ExprNodeCallMethod::ExprNodeCallMethod(ExprNodeP obj, PathNode method, ::std::vector<ExprNodeP> args)
    : m_val(::std::move(obj))
    , m_method(::std::move(method))
    , m_args(::std::move(args)) {
}
ExprNodeCallObject::ExprNodeCallObject(ExprNodeP val, ::std::vector<ExprNodeP>&& args)
    : m_val(::std::move(val))
    , m_args(::std::move(args)) {
}
ExprNodeLoop::ExprNodeLoop()
    : m_label("") {
}
ExprNodeLoop::ExprNodeLoop(Ident label, ExprNodeP code)
    : m_label(::std::move(label))
    , m_code(::std::move(code)) {
}
ExprNodeFor::ExprNodeFor(Ident label, AST::Pattern pattern, ExprNodeP val, ExprNodeP code)
    : m_label(::std::move(label))
    , m_pattern(::std::move(pattern))
    , m_value(::std::move(val))
    , m_code(::std::move(code)) {
}
ExprNodeWhile::ExprNodeWhile(Ident label, std::vector<IfLet_Condition> conditions, ExprNodeP code)
    : m_label(::std::move(label))
    , m_conditions(::std::move(conditions))
    , m_code(::std::move(code)) {
}
ExprNodeMatchArm::ExprNodeMatchArm() {
}
ExprNodeMatchArm::ExprNodeMatchArm(::std::vector<Pattern> patterns, std::vector<IfLet_Condition> guard, ExprNodeP code)
    : m_patterns(mv$(patterns))
    , m_guard(mv$(guard))
    , m_code(mv$(code)) {
}
ExprNodeMatch::ExprNodeMatch(ExprNodeP val, ::std::vector<ExprNodeMatchArm> arms)
    : m_val(::std::move(val))
    , m_arms(::std::move(arms)) {
}
ExprNodeIf::ExprNodeIf(std::vector<Arm> arms, ExprNodeP else_code)
    : m_arms(::std::move(arms))
    , m_else(::std::move(else_code)) {
}
ExprNodeInteger::ExprNodeInteger(U128 value, enum eCoreType datatype)
    : m_datatype(datatype)
    , m_value(value) {
}
ExprNodeFloat::ExprNodeFloat(FloatValue value, enum eCoreType datatype)
    : m_datatype(datatype)
    , m_value(value) {
}
ExprNodeBool::ExprNodeBool(bool value)
    : m_value(value) {
}
ExprNodeString::ExprNodeString(::std::string value, Ident::Hygiene h)
    : m_value(::std::move(value))
    , m_hygiene(::std::move(h)) {
}
ExprNodeByteString::ExprNodeByteString(::std::string value)
    : m_value(::std::move(value)) {
}
ExprNodeCString::ExprNodeCString(::std::string value)
    : m_value(::std::move(value)) {
}
ExprNodeStructLiteral::ExprNodeStructLiteral(Path path, ExprNodeP base_value, t_values&& values)
    : m_path(std::move(path))
    , m_base_value(std::move(base_value))
    , m_values(std::move(values)) {
}
ExprNodeStructLiteralPattern::ExprNodeStructLiteralPattern(Path path, t_values&& values)
    : m_path(std::move(path))
    , m_values(std::move(values)) {
}
ExprNodeArray::ExprNodeArray(::std::vector<ExprNodeP> vals)
    : m_values(::std::move(vals)) {
}
ExprNodeArray::ExprNodeArray(ExprNodeP val, ExprNodeP size)
    : m_size(::std::move(size)) {
    m_values.push_back(::std::move(val));
}
ExprNodeTuple::ExprNodeTuple(::std::vector<ExprNodeP> vals)
    : m_values(::std::move(vals)) {
}
ExprNodeNamedValue::ExprNodeNamedValue(Path path)
    : m_path(::std::move(path)) {
}
ExprNodeField::ExprNodeField(ExprNodeP obj, RcString name)
    : m_obj(::std::move(obj))
    , m_name(::std::move(name)) {
}
ExprNodeIndex::ExprNodeIndex(ExprNodeP obj, ExprNodeP idx)
    : m_obj(::std::move(obj))
    , m_idx(::std::move(idx)) {
}
ExprNodeDeref::ExprNodeDeref(ExprNodeP value)
    : m_value(::std::move(value)) {
}
ExprNodeCast::ExprNodeCast(ExprNodeP value, TypeRef&& dst_type)
    : m_value(::std::move(value))
    , m_type(::std::move(dst_type)) {
}
ExprNodeTypeAnnotation::ExprNodeTypeAnnotation(ExprNodeP value, TypeRef&& dst_type)
    : m_value(::std::move(value))
    , m_type(::std::move(dst_type)) {
}
ExprNodeBinOp::ExprNodeBinOp(Type type, ExprNodeP left, ExprNodeP right)
    : m_type(type)
    , m_left(::std::move(left))
    , m_right(::std::move(right)) {
}
ExprNodeUniOp::ExprNodeUniOp(Type type, ExprNodeP value)
    : m_type(type)
    , m_value(::std::move(value)) {
}
ExprNodeMacroDefinition::ExprNodeMacroDefinition(unsigned int definition_id, Ident::Hygiene token_hygiene, Ident::Hygiene definition_hygiene)
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
