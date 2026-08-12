#include "ast_expr.h"
#include "ast_ast.h"
#include <cctype>

namespace AST {

    ExprNodeP::~ExprNodeP() {
        if (ptr) {
            delete ptr;
        }
        ptr = nullptr;
    }

    ExprNodeP::ExprNodeP(std::unique_ptr<ExprNode> node)
        : ptr(node.release())
    {
    }

    const char* ExprNodeP::type_name() const {
        return typeid(*ptr).name();
    }

    Expr::Expr(ExprNodeP node)
        : mNode(node.release())
    {
    }

    Expr::Expr(ExprNode* node)
        : mNode(node)
    {
    }

    Expr::Expr()
        : mNode(nullptr)
    {
    }

    void Expr::visit_nodes(NodeVisitor& v) {
        if (mNode) {
            mNode->visit(v);
        }
    }

    void Expr::visit_nodes(NodeVisitor& v) const {
        if (mNode) {
            assert(v.is_const());
            //const_cast<const ExprNode*>(m_node.get())->visit(v);
            mNode->visit(v);
        }
    }

    Expr Expr::clone() const {
        if (mNode) {
            return Expr(mNode->clone());
        } else {
            return Expr();
        }
    }

    ::std::ostream& operator<<(::std::ostream& os, const Expr& pat) {
        if (pat.mNode.get()) {
            return os << *pat.mNode;
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
            for (const auto& n : nodes) {
                os << *n.node << (n.has_semicolon ? ";" : "");
            }
            os << "}";
        },
        {
            ::std::vector<Line> newNodes;
            for (const auto& n : nodes) {
                newNodes.push_back({n.has_semicolon, n.node->clone()});
            }
            return NEWNODE(ExprNodeBlock, blockType, mv$(newNodes), localMod);
        }
    )

    NODE(ExprNodeAsyncBlock, { os << "async " << (isMove ? "move " : "") << *inner; }, { return NEWNODE(ExprNodeAsyncBlock, inner->clone(), isMove); })
    NODE(ExprNodeGeneratorBlock, { os << "gen " << (isMove ? "move " : "") << *inner; }, { return NEWNODE(ExprNodeGeneratorBlock, inner->clone(), isMove); })
    NODE(ExprNodeTry, { os << "try " << *inner; }, { return NEWNODE(ExprNodeTry, inner->clone()); })

    NODE(
        ExprNodeMacro,
        {
            os << mPath << "!";
            if (ident.size() > 0) {
                os << " " << ident << " ";
            }
            os << "(" << " /*TODO*/ " << ")";
        },
        { return NEWNODE(ExprNodeMacro, AST::Path(mPath), ident, tokens.clone(), isBraced, definitionHygiene); }
    )

    NODE(
        ExprNodeAsm,
        {
            os << "llvm_asm!( \"" << text << "\"";
            os << " :";
            for (const auto& v : output) {
                os << " \"" << v.name << "\" (" << *v.value << "),";
            }
            os << " :";
            for (const auto& v : input) {
                os << " \"" << v.name << "\" (" << *v.value << "),";
            }
            os << " :";
            for (const auto& v : clobbers) {
                os << " \"" << v << "\",";
            }
            os << " :";
            for (const auto& v : flags) {
                os << " \"" << v << "\",";
            }
            os << " )";
        },
        {
            ::std::vector<ExprNodeAsm::ValRef> outputs;
            for (const auto& v : output) {
                outputs.push_back(ExprNodeAsm::ValRef{v.name, v.value->clone()});
            }
            ::std::vector<ExprNodeAsm::ValRef> inputs;
            for (const auto& v : input) {
                inputs.push_back(ExprNodeAsm::ValRef{v.name, v.value->clone()});
            }
            return NEWNODE(ExprNodeAsm, text, mv$(outputs), mv$(inputs), clobbers, flags);
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
            for (const auto& l : lines) {
                l.fmt(os);
                os << ", ";
            }
            for (const auto& p : mParams) {
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

            for (const auto& p : mParams) {
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

            return NEWNODE(ExprNodeAsm2, options, lines, std::move(params));
        }
    )

    NODE(
        ExprNodeFlow,
        {
            switch (mType) {
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
            if (mValue) {
                os << " " << *mValue;
            }
        },
        { return NEWNODE(ExprNodeFlow, mType, target, mValue ? mValue->clone() : nullptr); }
    )

    NODE(
        ExprNodeLetBinding,
        {
            os << (isSuper ? "super let " : "let ") << pat << ": " << mType;
            if (mValue) {
                os << " = " << *mValue;
                if (elseNode) {
                    os << " else " << *elseNode;
                }
            }
        },
        { return NEWNODE(ExprNodeLetBinding, pat.clone(), mType.clone(), OPT_CLONE(mValue), OPT_CLONE(elseNode), isSuper); }
    )

    NODE(ExprNodeAssign, { os << *slot << " = " << *mValue; }, { return NEWNODE(ExprNodeAssign, op, slot->clone(), mValue->clone()); })

    NODE(
        ExprNodeCallPath,
        {
            os << mPath << "(";
            for (const auto& a : mArgs) {
                os << *a << ",";
            }
            os << ")";
        },
        {
            ::std::vector<ExprNodeP> args;
            for (const auto& a : mArgs) {
                args.push_back(a->clone());
            }
            return NEWNODE(ExprNodeCallPath, AST::Path(mPath), mv$(args));
        }
    )

    NODE(
        ExprNodeCallMethod,
        {
            os << "(" << *val << ")." << method << "(";
            for (const auto& a : mArgs) {
                os << *a << ",";
            }
            os << ")";
        },
        {
            ::std::vector<ExprNodeP> args;
            for (const auto& a : mArgs) {
                args.push_back(a->clone());
            }
            return NEWNODE(ExprNodeCallMethod, val->clone(), method, mv$(args));
        }
    )

    NODE(
        ExprNodeCallObject,
        {
            os << "(" << *val << ")(";
            for (const auto& a : mArgs) {
                os << *a << ",";
            }
            os << ")";
        },
        {
            ::std::vector<ExprNodeP> args;
            for (const auto& a : mArgs) {
                args.push_back(a->clone());
            }
            return NEWNODE(ExprNodeCallObject, val->clone(), mv$(args));
        }
    )

    NODE(ExprNodeLoop, { os << "LOOP [" << label << "] " << *mCode; }, { return NEWNODE(ExprNodeLoop, label, mCode->clone()); })

    NODE(
        ExprNodeFor,
        {
            os << "FOR [" << label << "] " << pattern << "in" << *mValue;
            os << " " << *mCode;
        },
        { return NEWNODE(ExprNodeFor, label, pattern.clone(), mValue->clone(), mCode->clone()); }
    )

    namespace {
        void fmt_iflet_conditions(::std::ostream& os, const ::std::vector<AST::IfLetCondition>& conditions) {
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

        ::std::vector<AST::IfLetCondition> cloneIfletConditions(const ::std::vector<AST::IfLetCondition>& conditions) {
            ::std::vector<AST::IfLetCondition> new_conds;
            new_conds.reserve(conditions.size());
            for (const auto& cond : conditions) {
                AST::IfLetCondition new_cond;
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
            if (label != "") {
                os << "'" << label << ": ";
            }
            os << "while ";
            fmt_iflet_conditions(os, conditions);
            os << " { " << *mCode << " }";
        },
        {
            auto new_conds = cloneIfletConditions(conditions);
            return NEWNODE(ExprNodeWhile, label, mv$(new_conds), mCode->clone());
        }
    )

    NODE(
        ExprNodeMatch,
        {
            os << "match (" << *val << ") {";
            for (const auto& arm : arms) {
                for (const auto& pat : arm.patterns) {
                    os << " " << pat;
                }
                if (arm.guard.size() > 0) {
                    os << " if ";
                    fmt_iflet_conditions(os, arm.guard);
                }

                os << " => " << *arm.mCode << ",";
            }
            os << "}";
        },
        {
            ::std::vector<ExprNodeMatchArm> newArms;
            for (const auto& arm : arms) {
                ::std::vector<AST::Pattern> patterns;
                for (const auto& pat : arm.patterns) {
                    patterns.push_back(pat.clone());
                }
                newArms.push_back(ExprNodeMatchArm(mv$(patterns), cloneIfletConditions(arm.guard), arm.mCode->clone()));
                newArms.back().mAttrs = arm.mAttrs.clone();
            }
            return NEWNODE(ExprNodeMatch, val->clone(), mv$(newArms));
        }
    )

    NODE(
        ExprNodeIf,
        {
            for (const auto& arm : arms) {
                if (&arm != arms.data()) {
                    os << " else ";
                }
                os << "if ";
                fmt_iflet_conditions(os, arm.conditions);
                os << " { " << *arm.body << " }";
            }
            if (elseNode) {
                os << " else { " << *elseNode << " }";
            }
        },
        {
            std::vector<Arm> newArms;
            newArms.reserve(arms.size());
            for (const auto& arm : arms) {
                newArms.push_back(ExprNodeIf::Arm{cloneIfletConditions(arm.conditions), arm.body->clone()});
            }
            return NEWNODE(ExprNodeIf, std::move(newArms), OPT_CLONE(elseNode));
        }
    )

    NODE(ExprNodeWildcardPattern, { os << "_"; }, { return NEWNODE(ExprNodeWildcardPattern); })
    NODE(
        ExprNodeInteger,
        {
            if (datatype == CORETYPE_CHAR) {
                os << "'\\u{" << ::std::hex << mValue << ::std::dec << "}'";
            } else {
                os << mValue;
                if (datatype == CORETYPE_ANY)
                    ;
                else {
                    os << "_" << coretypeName(datatype);
                }
            }
        },
        { return NEWNODE(ExprNodeInteger, mValue, datatype); }
    )
    NODE(ExprNodeFloat, { os << mValue << "_" << datatype; }, { return NEWNODE(ExprNodeFloat, mValue, datatype); })
    NODE(ExprNodeBool, { os << mValue; }, { return NEWNODE(ExprNodeBool, mValue); })
    NODE(ExprNodeString, { os << "\"" << mValue << "\""; }, { return NEWNODE(ExprNodeString, mValue, mHygiene); })
    NODE(ExprNodeByteString, { os << "b\"" << mValue << "\""; }, { return NEWNODE(ExprNodeByteString, mValue); })
    NODE(ExprNodeCString, { os << "c\"" << mValue << "\""; }, { return NEWNODE(ExprNodeCString, mValue); })

    NODE(
        ExprNodeClosure,
        {
            if (isPinned) {
                os << "static ";
            }
            if (isMove) {
                os << "move ";
            }
            os << "|";
            for (const auto& a : mArgs) {
                os << a.first << ": " << a.second << ",";
            }
            os << "|";
            os << "->" << returnType;
            os << " " << *mCode;
        },
        {
            ExprNodeClosure::argsT args;
            for (const auto& a : mArgs) {
                args.push_back(::std::make_pair(a.first.clone(), a.second.clone()));
            }
            return NEWNODE(ExprNodeClosure, mv$(args), returnType.clone(), mCode->clone(), isMove, isPinned);
        }
    );

    NODE(
        ExprNodeStructLiteral,
        {
            os << mPath << " { ";
            for (const auto& v : values) {
                os << v.name << ": " << *v.value << ", ";
            }
            if (baseValue) {
                os << ".." << *baseValue;
            }
            os << "}";
        },
        {
            ExprNodeStructLiteral::t_values vals;

            for (const auto& v : values) {
                vals.push_back({v.attrs.clone(), v.name, v.value->clone()});
            }

            return NEWNODE(ExprNodeStructLiteral, AST::Path(mPath), OPT_CLONE(baseValue), mv$(vals));
        }
    )
    NODE(
        ExprNodeStructLiteralPattern,
        {
            os << mPath << " /*pat*/ { ";
            for (const auto& v : values) {
                os << v.name << ": " << *v.value << ", ";
            }
            os << ".. }";
        },
        {
            ExprNodeStructLiteral::t_values vals;

            for (const auto& v : values) {
                vals.push_back({v.attrs.clone(), v.name, v.value->clone()});
            }

            return NEWNODE(ExprNodeStructLiteralPattern, AST::Path(mPath), mv$(vals));
        }
    )

    NODE(
        ExprNodeArray,
        {
            os << "[";
            if (mSize.get()) {
                os << *values[0] << "; " << *mSize;
            } else {
                for (const auto& a : values) {
                    os << *a << ",";
                }
            }
            os << "]";
        },
        {
            if (mSize.get()) {
                return NEWNODE(ExprNodeArray, values[0]->clone(), mSize->clone());
            } else {
                ::std::vector<ExprNodeP> nodes;
                for (const auto& n : values) {
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
            for (const auto& a : values) {
                os << *a << ",";
            }
            os << ")";
        },
        {
            ::std::vector<ExprNodeP> nodes;
            for (const auto& n : values) {
                nodes.push_back(n->clone());
            }
            return NEWNODE(ExprNodeTuple, mv$(nodes));
        }
    )

    NODE(
        ExprNodeNamedValue,
        {
            mPath.print_pretty(os, false);
            //os << m_path;
        },
        { return NEWNODE(ExprNodeNamedValue, AST::Path(mPath)); }
    )

    NODE(ExprNodeField, { os << "(" << *obj << ")." << mName; }, { return NEWNODE(ExprNodeField, obj->clone(), mName); })

    NODE(ExprNodeIndex, { os << "(" << *obj << ")[" << *idx << "]"; }, { return NEWNODE(ExprNodeIndex, obj->clone(), idx->clone()); })

    NODE(ExprNodeDeref, { os << "*(" << *mValue << ")"; }, { return NEWNODE(ExprNodeDeref, mValue->clone()); });

    NODE(ExprNodeCast, { os << "(" << *mValue << " as " << mType << ")"; }, { return NEWNODE(ExprNodeCast, mValue->clone(), mType.clone()); })
    NODE(ExprNodeTypeAnnotation, { os << "(" << *mValue << ": " << mType << ")"; }, { return NEWNODE(ExprNodeTypeAnnotation, mValue->clone(), mType.clone()); })

    NODE(
        ExprNodeBinOp,
        {
            if (mType == RANGE_INC) {
                os << "(";
                if (left) {
                    os << *left << " ";
                }
                os << "... " << *right;
                os << ")";
                return;
            }
            if (mType == RANGE) {
                os << "(";
                if (left) {
                    os << *left;
                }
                os << "..";
                if (right) {
                    os << " " << *right;
                }
                os << ")";
                return;
            }
            os << "(" << *left << " ";
            switch (mType) {
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
            os << " " << *right << ")";
        },
        { return NEWNODE(ExprNodeBinOp, mType, OPT_CLONE(left), OPT_CLONE(right)); }
    )

    NODE(
        ExprNodeUniOp,
        {
            switch (mType) {
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
                    os << "(" << *mValue << "?)";
                    return;
                case AWait:
                    os << "((" << *mValue << ").await)";
                    return;
            }
            os << *mValue << ")";
        },
        { return NEWNODE(ExprNodeUniOp, mType, mValue->clone()); }
    )

    NODE(
        ExprNodeMacroDefinition,
        { os << "/* macro definition #" << definitionId << " */"; },
        { return NEWNODE(ExprNodeMacroDefinition, definitionId, tokenHygiene, definitionHygiene); }
    )

#define NV(type, actions)                                              \
    void NodeVisitorDef::visit(type& node) { /*DEBUG("DEF - "#type);*/ \
        actions                                                        \
    }
    //  void NodeVisitorDef::visit(const type& node) { DEBUG("DEF - "#type" (const)"); actions }

    NV(ExprNodeBlock, {
        //INDENT();
        for (auto& child : node.nodes) {
            visit(child.node);
        }
        //UNINDENT();
    })
    NV(ExprNodeAsyncBlock, { visit(node.inner); })
    NV(ExprNodeGeneratorBlock, { visit(node.inner); })
    NV(ExprNodeTry, { visit(node.inner); })
    NV(ExprNodeMacro, { BUG(node.span(), "Hit unexpanded macro in expression - " << node); })
    NV(ExprNodeAsm, {
        for (auto& v : node.output) {
            visit(v.value);
        }
        for (auto& v : node.input) {
            visit(v.value);
        }
    })
    NV(ExprNodeAsm2, {
        for (auto& v : node.mParams) {
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
    NV(ExprNodeFlow, { visit(node.mValue); })
    NV(ExprNodeLetBinding, {
        // TODO: Handle recurse into Let pattern?
        visit(node.mValue);
        visit(node.elseNode);
    })
    NV(ExprNodeAssign, {
        INDENT();
        visit(node.slot);
        visit(node.mValue);
        UNINDENT();
    })
    NV(ExprNodeCallPath, {
        INDENT();
        for (auto& arg : node.mArgs) {
            visit(arg);
        }
        UNINDENT();
    })
    NV(ExprNodeCallMethod, {
        INDENT();
        visit(node.val);
        for (auto& arg : node.mArgs) {
            visit(arg);
        }
        UNINDENT();
    })
    NV(ExprNodeCallObject, {
        INDENT();
        visit(node.val);
        for (auto& arg : node.mArgs) {
            visit(arg);
        }
        UNINDENT();
    })
    NV(ExprNodeLoop, {
        INDENT();
        visit(node.mCode);
        UNINDENT();
    })
    NV(ExprNodeFor, {
        INDENT();
        visit(node.mValue);
        visit(node.mCode);
        UNINDENT();
    })
    NV(ExprNodeWhile, {
        INDENT();
        for (auto& c : node.conditions) {
            visit(c.value);
        }
        visit(node.mCode);
        UNINDENT();
    })
    NV(ExprNodeMatch, {
        INDENT();
        visit(node.val);
        for (auto& arm : node.arms) {
            for (auto& c : arm.guard) {
                visit(c.value);
            }
            visit(arm.mCode);
        }
        UNINDENT();
    })
    NV(ExprNodeIf, {
        INDENT();
        for (auto& a : node.arms) {
            for (auto& c : a.conditions) {
                visit(c.value);
            }
            visit(a.body);
        }
        visit(node.elseNode);
        UNINDENT();
    })

    NV(ExprNodeWildcardPattern, { (void)node; })
    NV(ExprNodeInteger, { (void)node; })
    NV(ExprNodeFloat, { (void)node; })
    NV(ExprNodeBool, { (void)node; })
    NV(ExprNodeString, { (void)node; })
    NV(ExprNodeByteString, { (void)node; })
    NV(ExprNodeCString, { (void)node; })

    NV(ExprNodeClosure, { visit(node.mCode); });
    NV(ExprNodeStructLiteral, {
        visit(node.baseValue);
        for (auto& val : node.values) {
            visit(val.value);
        }
    })
    NV(ExprNodeStructLiteralPattern, {
        for (auto& val : node.values) {
            visit(val.value);
        }
    })
    NV(ExprNodeArray, {
        visit(node.mSize);
        for (auto& val : node.values) {
            visit(val);
        }
    })
    NV(ExprNodeTuple, {
        for (auto& val : node.values) {
            visit(val);
        }
    })
    NV(ExprNodeNamedValue, {
        (void)node;
        // LEAF
    })

    NV(ExprNodeField, { visit(node.obj); })
    NV(ExprNodeIndex, {
        visit(node.obj);
        visit(node.idx);
    })
    NV(ExprNodeDeref, { visit(node.mValue); })
    NV(ExprNodeCast, { visit(node.mValue); })
    NV(ExprNodeTypeAnnotation, { visit(node.mValue); })
    NV(ExprNodeBinOp, {
        visit(node.left);
        visit(node.right);
    })
    NV(ExprNodeUniOp, { visit(node.mValue); })
    NV(ExprNodeMacroDefinition, {})
#undef NV

};

namespace AST {

void ExprNode::set_attrs(AttributeList&& mi) {
    for (auto& i : mi.mItems) {
        mAttrs.mItems.push_back(mv$(i));
    }
    mi.mItems.clear();
}
ExprNodeBlock::ExprNodeBlock(::std::vector<Line> nodes)
    : blockType(Type::Bare)
    , label("")
    , localMod()
    , nodes(::std::move(nodes)) {
}
/// Shortcut for a block that returns a contained node
ExprNodeBlock::ExprNodeBlock(ExprNodeP value)
    : ExprNodeBlock() {
    set_span(value->span());
    nodes.push_back({false, std::move(value)});
}
ExprNodeBlock::ExprNodeBlock(Type type, ::std::vector<Line> nodes, ::std::shared_ptr<AST::Module> local_mod)
    : blockType(type)
    , label("")
    , localMod(::std::move(local_mod))
    , nodes(::std::move(nodes)) {
}
ExprNodeAsyncBlock::ExprNodeAsyncBlock(ExprNodeP inner, bool is_move)
    : inner(std::move(inner))
    , isMove(is_move) {
}
ExprNodeGeneratorBlock::ExprNodeGeneratorBlock(ExprNodeP inner, bool is_move)
    : inner(std::move(inner))
    , isMove(is_move) {
}
ExprNodeTry::ExprNodeTry(ExprNodeP inner)
    : inner(::std::move(inner)) {
}
ExprNodeMacro::ExprNodeMacro(AST::Path name, RcString ident, ::TokenTree&& tokens, bool is_braced, Ident::Hygiene definition_hygiene)
    : mPath(::std::move(name))
    , ident(ident)
    , tokens(::std::move(tokens))
    , isBraced(is_braced)
    , definitionHygiene(::std::move(definition_hygiene)) {
}
ExprNodeAsm::ExprNodeAsm(::std::string text, ::std::vector<ValRef> output, ::std::vector<ValRef> input, ::std::vector<::std::string> clobbers, ::std::vector<::std::string> flags)
    : text(::std::move(text))
    , output(::std::move(output))
    , input(::std::move(input))
    , clobbers(::std::move(clobbers))
    , flags(::std::move(flags)) {
}
ExprNodeAsm2::ExprNodeAsm2(AsmCommon::Options options, std::vector<AsmCommon::Line> lines, std::vector<Param> params)
    : options(options)
    , lines(::std::move(lines))
    , mParams(::std::move(params)) {
}
ExprNodeFlow::ExprNodeFlow(Type type, Ident target, ExprNodeP value)
    : mType(type)
    , target(::std::move(target))
    , mValue(::std::move(value)) {
}
ExprNodeLetBinding::ExprNodeLetBinding(Pattern pat, TypeRef type, ExprNodeP value, ExprNodeP elseArm, bool is_super)
    : pat(::std::move(pat))
    , mType(::std::move(type))
    , mValue(::std::move(value))
    , elseNode(::std::move(elseArm))
    , isSuper(is_super) {
}
ExprNodeAssign::ExprNodeAssign()
    : op(NONE) {
}
ExprNodeAssign::ExprNodeAssign(Operation op, ExprNodeP slot, ExprNodeP value)
    : op(op)
    , slot(::std::move(slot))
    , mValue(::std::move(value)) {
}
ExprNodeCallPath::ExprNodeCallPath(Path&& path, ::std::vector<ExprNodeP>&& args)
    : mPath(::std::move(path))
    , mArgs(::std::move(args)) {
}
ExprNodeCallMethod::ExprNodeCallMethod(ExprNodeP obj, PathNode method, ::std::vector<ExprNodeP> args)
    : val(::std::move(obj))
    , method(::std::move(method))
    , mArgs(::std::move(args)) {
}
ExprNodeCallObject::ExprNodeCallObject(ExprNodeP val, ::std::vector<ExprNodeP>&& args)
    : val(::std::move(val))
    , mArgs(::std::move(args)) {
}
ExprNodeLoop::ExprNodeLoop()
    : label("") {
}
ExprNodeLoop::ExprNodeLoop(Ident label, ExprNodeP code)
    : label(::std::move(label))
    , mCode(::std::move(code)) {
}
ExprNodeFor::ExprNodeFor(Ident label, AST::Pattern pattern, ExprNodeP val, ExprNodeP code)
    : label(::std::move(label))
    , pattern(::std::move(pattern))
    , mValue(::std::move(val))
    , mCode(::std::move(code)) {
}
ExprNodeWhile::ExprNodeWhile(Ident label, std::vector<IfLetCondition> conditions, ExprNodeP code)
    : label(::std::move(label))
    , conditions(::std::move(conditions))
    , mCode(::std::move(code)) {
}
ExprNodeMatchArm::ExprNodeMatchArm() {
}
ExprNodeMatchArm::ExprNodeMatchArm(::std::vector<Pattern> patterns, std::vector<IfLetCondition> guard, ExprNodeP code)
    : patterns(mv$(patterns))
    , guard(mv$(guard))
    , mCode(mv$(code)) {
}
ExprNodeMatch::ExprNodeMatch(ExprNodeP val, ::std::vector<ExprNodeMatchArm> arms)
    : val(::std::move(val))
    , arms(::std::move(arms)) {
}
ExprNodeIf::ExprNodeIf(std::vector<Arm> arms, ExprNodeP elseCode)
    : arms(::std::move(arms))
    , elseNode(::std::move(elseCode)) {
}
ExprNodeInteger::ExprNodeInteger(U128 value, enum eCoreType datatype)
    : datatype(datatype)
    , mValue(value) {
}
ExprNodeFloat::ExprNodeFloat(FloatValue value, enum eCoreType datatype)
    : datatype(datatype)
    , mValue(value) {
}
ExprNodeBool::ExprNodeBool(bool value)
    : mValue(value) {
}
ExprNodeString::ExprNodeString(::std::string value, Ident::Hygiene h)
    : mValue(::std::move(value))
    , mHygiene(::std::move(h)) {
}
ExprNodeByteString::ExprNodeByteString(::std::string value)
    : mValue(::std::move(value)) {
}
ExprNodeCString::ExprNodeCString(::std::string value)
    : mValue(::std::move(value)) {
}
ExprNodeStructLiteral::ExprNodeStructLiteral(Path path, ExprNodeP base_value, t_values&& values)
    : mPath(std::move(path))
    , baseValue(std::move(base_value))
    , values(std::move(values)) {
}
ExprNodeStructLiteralPattern::ExprNodeStructLiteralPattern(Path path, t_values&& values)
    : mPath(std::move(path))
    , values(std::move(values)) {
}
ExprNodeArray::ExprNodeArray(::std::vector<ExprNodeP> vals)
    : values(::std::move(vals)) {
}
ExprNodeArray::ExprNodeArray(ExprNodeP val, ExprNodeP size)
    : mSize(::std::move(size)) {
    values.push_back(::std::move(val));
}
ExprNodeTuple::ExprNodeTuple(::std::vector<ExprNodeP> vals)
    : values(::std::move(vals)) {
}
ExprNodeNamedValue::ExprNodeNamedValue(Path path)
    : mPath(::std::move(path)) {
}
ExprNodeField::ExprNodeField(ExprNodeP obj, RcString name)
    : obj(::std::move(obj))
    , mName(::std::move(name)) {
}
ExprNodeIndex::ExprNodeIndex(ExprNodeP obj, ExprNodeP idx)
    : obj(::std::move(obj))
    , idx(::std::move(idx)) {
}
ExprNodeDeref::ExprNodeDeref(ExprNodeP value)
    : mValue(::std::move(value)) {
}
ExprNodeCast::ExprNodeCast(ExprNodeP value, TypeRef&& dst_type)
    : mValue(::std::move(value))
    , mType(::std::move(dst_type)) {
}
ExprNodeTypeAnnotation::ExprNodeTypeAnnotation(ExprNodeP value, TypeRef&& dst_type)
    : mValue(::std::move(value))
    , mType(::std::move(dst_type)) {
}
ExprNodeBinOp::ExprNodeBinOp(Type type, ExprNodeP left, ExprNodeP right)
    : mType(type)
    , left(::std::move(left))
    , right(::std::move(right)) {
}
ExprNodeUniOp::ExprNodeUniOp(Type type, ExprNodeP value)
    : mType(type)
    , mValue(::std::move(value)) {
}
ExprNodeMacroDefinition::ExprNodeMacroDefinition(unsigned int definition_id, Ident::Hygiene token_hygiene, Ident::Hygiene definition_hygiene)
    : definitionId(definition_id)
    , tokenHygiene(::std::move(token_hygiene))
    , definitionHygiene(::std::move(definition_hygiene)) {
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
