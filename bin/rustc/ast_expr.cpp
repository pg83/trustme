#include "ast_expr.h"

#include "ast_ast.h"

#include <cctype>

ASTExprNodeP::~ASTExprNodeP() {
    if (ptr) {
        delete ptr;
    }
    ptr = nullptr;
}

ASTExprNodeP::ASTExprNodeP(std::unique_ptr<ASTExprNode> node)
    : ptr(node.release())
{
}

const char* ASTExprNodeP::typeName() const {
    return typeid(*ptr).name();
}

ASTExpr::ASTExpr(ASTExprNodeP node)
    : mNode(node.release())
{
}

ASTExpr::ASTExpr(ASTExprNode* node)
    : mNode(node)
{
}

ASTExpr::ASTExpr()
    : mNode(nullptr)
{
}

void ASTExpr::visitNodes(ASTNodeVisitor& v) {
    if (mNode) {
        mNode->visit(v);
    }
}

void ASTExpr::visitNodes(ASTNodeVisitor& v) const {
    if (mNode) {
        assert(v.isConst());
        mNode->visit(v);
    }
}

ASTExpr ASTExpr::clone() const {
    if (mNode) {
        return ASTExpr(mNode->clone());
    } else {
        return ASTExpr();
    }
}

::std::ostream& operator<<(::std::ostream& os, const ASTExpr& pat) {
    if (pat.mNode.get()) {
        return os << *pat.mNode;
    } else {
        return os << "/* null */";
    }
}

::std::ostream& operator<<(::std::ostream& os, const ASTExprNode& node) {
    assert(static_cast<const void*>(&node) != nullptr);
    node.print(os);
    return os;
}

ASTExprNode::~ASTExprNode() {
}

unsigned int ASTExprNodeBlock::nodeKind() const {
    return ASTExprNodeBlock::kind;
}

unsigned int ASTExprNodeAsyncBlock::nodeKind() const {
    return ASTExprNodeAsyncBlock::kind;
}

unsigned int ASTExprNodeGeneratorBlock::nodeKind() const {
    return ASTExprNodeGeneratorBlock::kind;
}

unsigned int ASTExprNodeTry::nodeKind() const {
    return ASTExprNodeTry::kind;
}

unsigned int ASTExprNodeMacro::nodeKind() const {
    return ASTExprNodeMacro::kind;
}

unsigned int ASTExprNodeAsm::nodeKind() const {
    return ASTExprNodeAsm::kind;
}

unsigned int ASTExprNodeAsm2::nodeKind() const {
    return ASTExprNodeAsm2::kind;
}

unsigned int ASTExprNodeFlow::nodeKind() const {
    return ASTExprNodeFlow::kind;
}

unsigned int ASTExprNodeLetBinding::nodeKind() const {
    return ASTExprNodeLetBinding::kind;
}

unsigned int ASTExprNodeAssign::nodeKind() const {
    return ASTExprNodeAssign::kind;
}

unsigned int ASTExprNodeCallPath::nodeKind() const {
    return ASTExprNodeCallPath::kind;
}

unsigned int ASTExprNodeCallMethod::nodeKind() const {
    return ASTExprNodeCallMethod::kind;
}

unsigned int ASTExprNodeCallObject::nodeKind() const {
    return ASTExprNodeCallObject::kind;
}

unsigned int ASTExprNodeLoop::nodeKind() const {
    return ASTExprNodeLoop::kind;
}

unsigned int ASTExprNodeFor::nodeKind() const {
    return ASTExprNodeFor::kind;
}

unsigned int ASTExprNodeWhile::nodeKind() const {
    return ASTExprNodeWhile::kind;
}

unsigned int ASTExprNodeMatch::nodeKind() const {
    return ASTExprNodeMatch::kind;
}

unsigned int ASTExprNodeIf::nodeKind() const {
    return ASTExprNodeIf::kind;
}

unsigned int ASTExprNodeWildcardPattern::nodeKind() const {
    return ASTExprNodeWildcardPattern::kind;
}

unsigned int ASTExprNodeInteger::nodeKind() const {
    return ASTExprNodeInteger::kind;
}

unsigned int ASTExprNodeFloat::nodeKind() const {
    return ASTExprNodeFloat::kind;
}

unsigned int ASTExprNodeBool::nodeKind() const {
    return ASTExprNodeBool::kind;
}

unsigned int ASTExprNodeString::nodeKind() const {
    return ASTExprNodeString::kind;
}

unsigned int ASTExprNodeByteString::nodeKind() const {
    return ASTExprNodeByteString::kind;
}

unsigned int ASTExprNodeCString::nodeKind() const {
    return ASTExprNodeCString::kind;
}

unsigned int ASTExprNodeClosure::nodeKind() const {
    return ASTExprNodeClosure::kind;
}

unsigned int ASTExprNodeStructLiteral::nodeKind() const {
    return ASTExprNodeStructLiteral::kind;
}

unsigned int ASTExprNodeStructLiteralPattern::nodeKind() const {
    return ASTExprNodeStructLiteralPattern::kind;
}

unsigned int ASTExprNodeArray::nodeKind() const {
    return ASTExprNodeArray::kind;
}

unsigned int ASTExprNodeTuple::nodeKind() const {
    return ASTExprNodeTuple::kind;
}

unsigned int ASTExprNodeNamedValue::nodeKind() const {
    return ASTExprNodeNamedValue::kind;
}

unsigned int ASTExprNodeField::nodeKind() const {
    return ASTExprNodeField::kind;
}

unsigned int ASTExprNodeIndex::nodeKind() const {
    return ASTExprNodeIndex::kind;
}

unsigned int ASTExprNodeDeref::nodeKind() const {
    return ASTExprNodeDeref::kind;
}

unsigned int ASTExprNodeCast::nodeKind() const {
    return ASTExprNodeCast::kind;
}

unsigned int ASTExprNodeTypeAnnotation::nodeKind() const {
    return ASTExprNodeTypeAnnotation::kind;
}

unsigned int ASTExprNodeBinOp::nodeKind() const {
    return ASTExprNodeBinOp::kind;
}

unsigned int ASTExprNodeUniOp::nodeKind() const {
    return ASTExprNodeUniOp::kind;
}

unsigned int ASTExprNodeMacroDefinition::nodeKind() const {
    return ASTExprNodeMacroDefinition::kind;
}

#define NODE(class, _print, _clone)          \
    void class ::visit(ASTNodeVisitor& nv) { \
        nv.visit(*this);                     \
    }                                        \
    void class ::print(::std::ostream& os) const _print ASTExprNodeP class ::clone() const _clone
#define OPT_CLONE(node) (node.get() ? node->clone() : ASTExprNodeP())

namespace {
    static inline ASTExprNodeP mkExprnodep(const Span& pos, ASTExprNode* en) {
        en->setSpan(pos);
        return ASTExprNodeP(en);
    }

#define NEWNODE(type, ...) mkExprnodep(span(), new type(__VA_ARGS__))
}

NODE(
    ASTExprNodeBlock,
    {
        os << "{";
        for (const auto& n : nodes) {
            os << *n.node << (n.hasSemicolon ? ";" : "");
        }
        os << "}";
    },
    {
        ::std::vector<Line> newNodes;
        for (const auto& n : nodes) {
            newNodes.push_back({n.hasSemicolon, n.node->clone()});
        }
        return NEWNODE(ASTExprNodeBlock, blockType, mv$(newNodes), localMod);
    }
)

NODE(ASTExprNodeAsyncBlock, { os << "async " << (isMove ? "move " : "") << *inner; }, { return NEWNODE(ASTExprNodeAsyncBlock, inner->clone(), isMove); })
NODE(ASTExprNodeGeneratorBlock, { os << "gen " << (isMove ? "move " : "") << *inner; }, { return NEWNODE(ASTExprNodeGeneratorBlock, inner->clone(), isMove); })
NODE(ASTExprNodeTry, { os << "try " << *inner; }, { return NEWNODE(ASTExprNodeTry, inner->clone()); })

NODE(
    ASTExprNodeMacro,
    {
        os << mPath << "!";
        if (ident.size() > 0) {
            os << " " << ident << " ";
        }
        os << "(" << " /*TODO*/ " << ")";
    },
    { return NEWNODE(ASTExprNodeMacro, ASTPath(mPath), ident, tokens.clone(), isBraced, definitionHygiene); }
)

NODE(
    ASTExprNodeAsm,
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
        ::std::vector<ASTExprNodeAsm::ValRef> outputs;
        for (const auto& v : output) {
            outputs.push_back(ASTExprNodeAsm::ValRef{v.name, v.value->clone()});
        }
        ::std::vector<ASTExprNodeAsm::ValRef> inputs;
        for (const auto& v : input) {
            inputs.push_back(ASTExprNodeAsm::ValRef{v.name, v.value->clone()});
        }
        return NEWNODE(ASTExprNodeAsm, text, mv$(outputs), mv$(inputs), clobbers, flags);
    }
)

namespace {
    void printFmtString(std::ostream& os, const std::string& s) {
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

void AsmLine::fmt(std::ostream& os) const {
    os << "\"";
    for (const auto& f : this->frags) {
        printFmtString(os, f.before);
        os << "{" << f.index;
        if (f.modifier) {
            os << ":" << f.modifier;
        }
        os << "}";
    }
    printFmtString(os, this->trailing);
    os << "\"";
}

NODE(
    ASTExprNodeAsm2,
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
                if (e.valIn) {
                    os << *e.valIn;
                } else {
                    os << "_";
                }
                os << " => ";
                if (e.valOut) {
                    os << *e.valOut;
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
                params.push_back(Param::make_Reg({e.dir, e.spec.clone(), e.valIn ? e.valIn->clone() : nullptr, e.valOut ? e.valOut->clone() : nullptr}));
            }
        }
        }

        return NEWNODE(ASTExprNodeAsm2, options, lines, std::move(params));
    }
)

NODE(
    ASTExprNodeFlow,
    {
        switch (mType) {
            case RETURN:
                os << "return";
                break;
            case TAILCALL:
                os << "become";
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
    { return NEWNODE(ASTExprNodeFlow, mType, target, mValue ? mValue->clone() : nullptr); }
)

NODE(
    ASTExprNodeLetBinding,
    {
        os << (isSuper ? "super let " : "let ") << pat << ": " << mType;
        if (mValue) {
            os << " = " << *mValue;
            if (elseNode) {
                os << " else " << *elseNode;
            }
        }
    },
    { return NEWNODE(ASTExprNodeLetBinding, pat.clone(), mType->clone(), OPT_CLONE(mValue), OPT_CLONE(elseNode), isSuper); }
)

NODE(ASTExprNodeAssign, { os << *slot << " = " << *mValue; }, { return NEWNODE(ASTExprNodeAssign, op, slot->clone(), mValue->clone()); })

NODE(
    ASTExprNodeCallPath,
    {
        os << mPath << "(";
        for (const auto& a : mArgs) {
            os << *a << ",";
        }
        os << ")";
    },
    {
        ::std::vector<ASTExprNodeP> args;
        for (const auto& a : mArgs) {
            args.push_back(a->clone());
        }
        return NEWNODE(ASTExprNodeCallPath, ASTPath(mPath), mv$(args));
    }
)

NODE(
    ASTExprNodeCallMethod,
    {
        os << "(" << *val << ")." << method << "(";
        for (const auto& a : mArgs) {
            os << *a << ",";
        }
        os << ")";
    },
    {
        ::std::vector<ASTExprNodeP> args;
        for (const auto& a : mArgs) {
            args.push_back(a->clone());
        }
        return NEWNODE(ASTExprNodeCallMethod, val->clone(), method, mv$(args));
    }
)

NODE(
    ASTExprNodeCallObject,
    {
        os << "(" << *val << ")(";
        for (const auto& a : mArgs) {
            os << *a << ",";
        }
        os << ")";
    },
    {
        ::std::vector<ASTExprNodeP> args;
        for (const auto& a : mArgs) {
            args.push_back(a->clone());
        }
        return NEWNODE(ASTExprNodeCallObject, val->clone(), mv$(args));
    }
)

NODE(ASTExprNodeLoop, { os << "LOOP [" << label << "] " << *mCode; }, { return NEWNODE(ASTExprNodeLoop, label, mCode->clone()); })

NODE(
    ASTExprNodeFor,
    {
        os << "FOR [" << label << "] " << pattern << "in" << *mValue;
        os << " " << *mCode;
    },
    { return NEWNODE(ASTExprNodeFor, label, pattern.clone(), mValue->clone(), mCode->clone()); }
)

namespace {
    void fmtIfletConditions(::std::ostream& os, const ::std::vector<ASTIfLetCondition>& conditions) {
        for (const auto& cond : conditions) {
            if (&cond != &conditions.front()) {
                os << " && ";
            }
            if (cond.optPat) {
                os << "let ";
                os << *cond.optPat << " = ";
            }
            os << "(" << *cond.value << ")";
        }
    }

    ::std::vector<ASTIfLetCondition> cloneIfletConditions(const ::std::vector<ASTIfLetCondition>& conditions) {
        ::std::vector<ASTIfLetCondition> newConds;
        newConds.reserve(conditions.size());
        for (const auto& cond : conditions) {
            ASTIfLetCondition newCond;
            if (cond.optPat) {
                newCond.optPat = std::make_unique<ASTPattern>(cond.optPat->clone());
            }
            newCond.value = cond.value->clone();
            newConds.push_back(std::move(newCond));
        }
        return newConds;
    }
}

NODE(
    ASTExprNodeWhile,
    {
        if (label != "") {
            os << "'" << label << ": ";
        }
        os << "while ";
        fmtIfletConditions(os, conditions);
        os << " { " << *mCode << " }";
    },
    {
        auto newConds = cloneIfletConditions(conditions);
        return NEWNODE(ASTExprNodeWhile, label, mv$(newConds), mCode->clone());
    }
)

NODE(
    ASTExprNodeMatch,
    {
        os << "match (" << *val << ") {";
        for (const auto& arm : arms) {
            for (const auto& pat : arm.patterns) {
                os << " " << pat;
            }
            if (arm.guard.size() > 0) {
                os << " if ";
                fmtIfletConditions(os, arm.guard);
            }

            os << " => " << *arm.mCode << ",";
        }
        os << "}";
    },
    {
        ::std::vector<ASTExprNodeMatchArm> newArms;
        for (const auto& arm : arms) {
            ::std::vector<ASTPattern> patterns;
            for (const auto& pat : arm.patterns) {
                patterns.push_back(pat.clone());
            }
            newArms.push_back(ASTExprNodeMatchArm(mv$(patterns), cloneIfletConditions(arm.guard), arm.mCode->clone()));
            newArms.back().mAttrs = arm.mAttrs.clone();
        }
        return NEWNODE(ASTExprNodeMatch, val->clone(), mv$(newArms));
    }
)

NODE(
    ASTExprNodeIf,
    {
        for (const auto& arm : arms) {
            if (&arm != arms.data()) {
                os << " else ";
            }
            os << "if ";
            fmtIfletConditions(os, arm.conditions);
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
            newArms.push_back(ASTExprNodeIf::Arm{cloneIfletConditions(arm.conditions), arm.body->clone()});
        }
        return NEWNODE(ASTExprNodeIf, std::move(newArms), OPT_CLONE(elseNode));
    }
)

NODE(ASTExprNodeWildcardPattern, { os << "_"; }, { return NEWNODE(ASTExprNodeWildcardPattern); })
NODE(
    ASTExprNodeInteger,
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
    { return NEWNODE(ASTExprNodeInteger, mValue, datatype); }
)
NODE(ASTExprNodeFloat, { os << mValue << "_" << datatype; }, { return NEWNODE(ASTExprNodeFloat, mValue, datatype); })
NODE(ASTExprNodeBool, { os << mValue; }, { return NEWNODE(ASTExprNodeBool, mValue); })
NODE(ASTExprNodeString, { os << "\"" << mValue << "\""; }, { return NEWNODE(ASTExprNodeString, mValue, mHygiene); })
NODE(ASTExprNodeByteString, { os << "b\"" << mValue << "\""; }, { return NEWNODE(ASTExprNodeByteString, mValue); })
NODE(ASTExprNodeCString, { os << "c\"" << mValue << "\""; }, { return NEWNODE(ASTExprNodeCString, mValue); })

NODE(
    ASTExprNodeClosure,
    {
        if (isPinned) {
            os << "static ";
        }
        if (isMove) {
            os << "move ";
        }
        if (isUse) {
            os << "use ";
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
        ASTExprNodeClosure::argsT args;
        for (const auto& a : mArgs) {
            args.push_back(::std::make_pair(a.first.clone(), a.second->clone()));
        }
        return NEWNODE(ASTExprNodeClosure, mv$(args), returnType->clone(), mCode->clone(), isMove, isUse, isPinned);
    }
);

NODE(
    ASTExprNodeStructLiteral,
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
        ASTExprNodeStructLiteral::tValues vals;

        for (const auto& v : values) {
            vals.push_back({v.attrs.clone(), v.name, v.value->clone()});
        }

        return NEWNODE(ASTExprNodeStructLiteral, ASTPath(mPath), OPT_CLONE(baseValue), mv$(vals));
    }
)
NODE(
    ASTExprNodeStructLiteralPattern,
    {
        os << mPath << " /*pat*/ { ";
        for (const auto& v : values) {
            os << v.name << ": " << *v.value << ", ";
        }
        os << ".. }";
    },
    {
        ASTExprNodeStructLiteral::tValues vals;

        for (const auto& v : values) {
            vals.push_back({v.attrs.clone(), v.name, v.value->clone()});
        }

        return NEWNODE(ASTExprNodeStructLiteralPattern, ASTPath(mPath), mv$(vals));
    }
)

NODE(
    ASTExprNodeArray,
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
            return NEWNODE(ASTExprNodeArray, values[0]->clone(), mSize->clone());
        } else {
            ::std::vector<ASTExprNodeP> nodes;
            for (const auto& n : values) {
                nodes.push_back(n->clone());
            }
            return NEWNODE(ASTExprNodeArray, mv$(nodes));
        }
    }
)

NODE(
    ASTExprNodeTuple,
    {
        os << "(";
        for (const auto& a : values) {
            os << *a << ",";
        }
        os << ")";
    },
    {
        ::std::vector<ASTExprNodeP> nodes;
        for (const auto& n : values) {
            nodes.push_back(n->clone());
        }
        return NEWNODE(ASTExprNodeTuple, mv$(nodes));
    }
)

NODE(ASTExprNodeNamedValue, { mPath.printPretty(os, false); }, { return NEWNODE(ASTExprNodeNamedValue, ASTPath(mPath)); })

NODE(ASTExprNodeField, { os << "(" << *obj << ")." << mName; }, { return NEWNODE(ASTExprNodeField, obj->clone(), mName); })

NODE(ASTExprNodeIndex, { os << "(" << *obj << ")[" << *idx << "]"; }, { return NEWNODE(ASTExprNodeIndex, obj->clone(), idx->clone()); })

NODE(ASTExprNodeDeref, { os << "*(" << *mValue << ")"; }, { return NEWNODE(ASTExprNodeDeref, mValue->clone()); });

NODE(ASTExprNodeCast, { os << "(" << *mValue << " as " << mType << ")"; }, { return NEWNODE(ASTExprNodeCast, mValue->clone(), mType->clone()); })
NODE(ASTExprNodeTypeAnnotation, { os << "(" << *mValue << ": " << mType << ")"; }, { return NEWNODE(ASTExprNodeTypeAnnotation, mValue->clone(), mType->clone()); })

NODE(
    ASTExprNodeBinOp,
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
    { return NEWNODE(ASTExprNodeBinOp, mType, OPT_CLONE(left), OPT_CLONE(right)); }
)

NODE(
    ASTExprNodeUniOp,
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
    { return NEWNODE(ASTExprNodeUniOp, mType, mValue->clone()); }
)

NODE(ASTExprNodeMacroDefinition, { os << "/* macro definition #" << definitionId << " */"; }, { return NEWNODE(ASTExprNodeMacroDefinition, definitionId, tokenHygiene, definitionHygiene); })

#define NV(type, actions)                                                 \
    void ASTNodeVisitorDef::visit(type& node) { /*DEBUG("DEF - "#type);*/ \
        actions                                                           \
    }
//  void NodeVisitorDef::visit(const type& node) { DEBUG("DEF - "#type" (const)"); actions }

NV(ASTExprNodeBlock, {
    for (auto& child : node.nodes) {
        visit(child.node);
    }
})
NV(ASTExprNodeAsyncBlock, { visit(node.inner); })
NV(ASTExprNodeGeneratorBlock, { visit(node.inner); })
NV(ASTExprNodeTry, { visit(node.inner); })
NV(ASTExprNodeMacro, { BUG(node.span(), "Hit unexpanded macro in expression - " << node); })
NV(ASTExprNodeAsm, {
    for (auto& v : node.output) {
        visit(v.value);
    }
    for (auto& v : node.input) {
        visit(v.value);
    }
})
NV(ASTExprNodeAsm2, {
    for (auto& v : node.mParams) {
        TU_MATCH_HDRA((v), {)
        TU_ARMA(Const, e) {
                visit(e);
            }
            TU_ARMA(Sym, e) {
            }
            TU_ARMA(RegSingle, e) {
                visit(e.val);
            }
            TU_ARMA(Reg, e) {
                visit(e.valIn);
                visit(e.valOut);
            }
        }
    }
})
NV(ASTExprNodeFlow, { visit(node.mValue); })
NV(ASTExprNodeLetBinding, {
    // TODO: Handle recurse into Let pattern?
    visit(node.mValue);
    visit(node.elseNode);
})
NV(ASTExprNodeAssign, {
    INDENT();
    visit(node.slot);
    visit(node.mValue);
    UNINDENT();
})
NV(ASTExprNodeCallPath, {
    INDENT();
    for (auto& arg : node.mArgs) {
        visit(arg);
    }
    UNINDENT();
})
NV(ASTExprNodeCallMethod, {
    INDENT();
    visit(node.val);
    for (auto& arg : node.mArgs) {
        visit(arg);
    }
    UNINDENT();
})
NV(ASTExprNodeCallObject, {
    INDENT();
    visit(node.val);
    for (auto& arg : node.mArgs) {
        visit(arg);
    }
    UNINDENT();
})
NV(ASTExprNodeLoop, {
    INDENT();
    visit(node.mCode);
    UNINDENT();
})
NV(ASTExprNodeFor, {
    INDENT();
    visit(node.mValue);
    visit(node.mCode);
    UNINDENT();
})
NV(ASTExprNodeWhile, {
    INDENT();
    for (auto& c : node.conditions) {
        visit(c.value);
    }
    visit(node.mCode);
    UNINDENT();
})
NV(ASTExprNodeMatch, {
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
NV(ASTExprNodeIf, {
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

NV(ASTExprNodeWildcardPattern, { (void)node; })
NV(ASTExprNodeInteger, { (void)node; })
NV(ASTExprNodeFloat, { (void)node; })
NV(ASTExprNodeBool, { (void)node; })
NV(ASTExprNodeString, { (void)node; })
NV(ASTExprNodeByteString, { (void)node; })
NV(ASTExprNodeCString, { (void)node; })

NV(ASTExprNodeClosure, { visit(node.mCode); });
NV(ASTExprNodeStructLiteral, {
    visit(node.baseValue);
    for (auto& val : node.values) {
        visit(val.value);
    }
})
NV(ASTExprNodeStructLiteralPattern, {
    for (auto& val : node.values) {
        visit(val.value);
    }
})
NV(ASTExprNodeArray, {
    visit(node.mSize);
    for (auto& val : node.values) {
        visit(val);
    }
})
NV(ASTExprNodeTuple, {
    for (auto& val : node.values) {
        visit(val);
    }
})
NV(ASTExprNodeNamedValue, {
    (void)node;
    // LEAF
})

NV(ASTExprNodeField, { visit(node.obj); })
NV(ASTExprNodeIndex, {
    visit(node.obj);
    visit(node.idx);
})
NV(ASTExprNodeDeref, { visit(node.mValue); })
NV(ASTExprNodeCast, { visit(node.mValue); })
NV(ASTExprNodeTypeAnnotation, { visit(node.mValue); })
NV(ASTExprNodeBinOp, {
    visit(node.left);
    visit(node.right);
})
NV(ASTExprNodeUniOp, { visit(node.mValue); })
NV(ASTExprNodeMacroDefinition, {})
#undef NV

void ASTExprNode::setAttrs(ASTAttributeList&& mi) {
    for (auto& i : mi.mItems) {
        mAttrs.mItems.push_back(mv$(i));
    }
    mi.mItems.clear();
}

ASTExprNodeBlock::ASTExprNodeBlock(::std::vector<Line> nodes)
    : blockType(Type::Bare)
    , label("")
    , localMod()
    , nodes(::std::move(nodes))
{
}

/// Shortcut for a block that returns a contained node
ASTExprNodeBlock::ASTExprNodeBlock(ASTExprNodeP value)
    : ASTExprNodeBlock()
{
    setSpan(value->span());
    nodes.push_back({false, std::move(value)});
}

ASTExprNodeBlock::ASTExprNodeBlock(Type type, ::std::vector<Line> nodes, ::std::shared_ptr<ASTModule> localMod)
    : blockType(type)
    , label("")
    , localMod(::std::move(localMod))
    , nodes(::std::move(nodes))
{
}

ASTExprNodeAsyncBlock::ASTExprNodeAsyncBlock(ASTExprNodeP inner, bool isMove)
    : inner(std::move(inner))
    , isMove(isMove)
{
}

ASTExprNodeGeneratorBlock::ASTExprNodeGeneratorBlock(ASTExprNodeP inner, bool isMove)
    : inner(std::move(inner))
    , isMove(isMove)
{
}

ASTExprNodeTry::ASTExprNodeTry(ASTExprNodeP inner)
    : inner(::std::move(inner))
{
}

ASTExprNodeMacro::ASTExprNodeMacro(ASTPath name, RcString ident, ::TokenTree&& tokens, bool isBraced, Ident::Hygiene definitionHygiene)
    : mPath(::std::move(name))
    , ident(ident)
    , tokens(::std::move(tokens))
    , isBraced(isBraced)
    , definitionHygiene(::std::move(definitionHygiene))
{
}

ASTExprNodeAsm::ASTExprNodeAsm(::std::string text, ::std::vector<ValRef> output, ::std::vector<ValRef> input, ::std::vector<::std::string> clobbers, ::std::vector<::std::string> flags)
    : text(::std::move(text))
    , output(::std::move(output))
    , input(::std::move(input))
    , clobbers(::std::move(clobbers))
    , flags(::std::move(flags))
{
}

ASTExprNodeAsm2::ASTExprNodeAsm2(AsmOptions options, std::vector<AsmLine> lines, std::vector<Param> params)
    : options(options)
    , lines(::std::move(lines))
    , mParams(::std::move(params))
{
}

ASTExprNodeFlow::ASTExprNodeFlow(Type type, Ident target, ASTExprNodeP value)
    : mType(type)
    , target(::std::move(target))
    , mValue(::std::move(value))
{
}

ASTExprNodeLetBinding::ASTExprNodeLetBinding(ASTPattern pat, ASTType* type, ASTExprNodeP value, ASTExprNodeP elseArm, bool isSuper)
    : pat(::std::move(pat))
    , mType(::std::move(type))
    , mValue(::std::move(value))
    , elseNode(::std::move(elseArm))
    , isSuper(isSuper)
{
}

ASTExprNodeAssign::ASTExprNodeAssign()
    : op(NONE)
{
}

ASTExprNodeAssign::ASTExprNodeAssign(Operation op, ASTExprNodeP slot, ASTExprNodeP value)
    : op(op)
    , slot(::std::move(slot))
    , mValue(::std::move(value))
{
}

ASTExprNodeCallPath::ASTExprNodeCallPath(ASTPath&& path, ::std::vector<ASTExprNodeP>&& args)
    : mPath(::std::move(path))
    , mArgs(::std::move(args))
{
}

ASTExprNodeCallMethod::ASTExprNodeCallMethod(ASTExprNodeP obj, ASTPathNode method, ::std::vector<ASTExprNodeP> args)
    : val(::std::move(obj))
    , method(::std::move(method))
    , mArgs(::std::move(args))
{
}

ASTExprNodeCallObject::ASTExprNodeCallObject(ASTExprNodeP val, ::std::vector<ASTExprNodeP>&& args)
    : val(::std::move(val))
    , mArgs(::std::move(args))
{
}

ASTExprNodeLoop::ASTExprNodeLoop()
    : label("")
{
}

ASTExprNodeLoop::ASTExprNodeLoop(Ident label, ASTExprNodeP code)
    : label(::std::move(label))
    , mCode(::std::move(code))
{
}

ASTExprNodeFor::ASTExprNodeFor(Ident label, ASTPattern pattern, ASTExprNodeP val, ASTExprNodeP code)
    : label(::std::move(label))
    , pattern(::std::move(pattern))
    , mValue(::std::move(val))
    , mCode(::std::move(code))
{
}

ASTExprNodeWhile::ASTExprNodeWhile(Ident label, std::vector<ASTIfLetCondition> conditions, ASTExprNodeP code)
    : label(::std::move(label))
    , conditions(::std::move(conditions))
    , mCode(::std::move(code))
{
}

ASTExprNodeMatchArm::ASTExprNodeMatchArm() {
}

ASTExprNodeMatchArm::ASTExprNodeMatchArm(::std::vector<ASTPattern> patterns, std::vector<ASTIfLetCondition> guard, ASTExprNodeP code)
    : patterns(mv$(patterns))
    , guard(mv$(guard))
    , mCode(mv$(code))
{
}

ASTExprNodeMatch::ASTExprNodeMatch(ASTExprNodeP val, ::std::vector<ASTExprNodeMatchArm> arms)
    : val(::std::move(val))
    , arms(::std::move(arms))
{
}

ASTExprNodeIf::ASTExprNodeIf(std::vector<Arm> arms, ASTExprNodeP elseCode)
    : arms(::std::move(arms))
    , elseNode(::std::move(elseCode))
{
}

ASTExprNodeInteger::ASTExprNodeInteger(U128 value, enum eCoreType datatype)
    : datatype(datatype)
    , mValue(value)
{
}

ASTExprNodeFloat::ASTExprNodeFloat(FloatValue value, enum eCoreType datatype)
    : datatype(datatype)
    , mValue(value)
{
}

ASTExprNodeBool::ASTExprNodeBool(bool value)
    : mValue(value)
{
}

ASTExprNodeString::ASTExprNodeString(::std::string value, Ident::Hygiene h)
    : mValue(::std::move(value))
    , mHygiene(::std::move(h))
{
}

ASTExprNodeByteString::ASTExprNodeByteString(::std::string value)
    : mValue(::std::move(value))
{
}

ASTExprNodeCString::ASTExprNodeCString(::std::string value)
    : mValue(::std::move(value))
{
}

ASTExprNodeStructLiteral::ASTExprNodeStructLiteral(ASTPath path, ASTExprNodeP baseValue, tValues&& values)
    : mPath(std::move(path))
    , baseValue(std::move(baseValue))
    , values(std::move(values))
{
}

ASTExprNodeStructLiteralPattern::ASTExprNodeStructLiteralPattern(ASTPath path, tValues&& values)
    : mPath(std::move(path))
    , values(std::move(values))
{
}

ASTExprNodeArray::ASTExprNodeArray(::std::vector<ASTExprNodeP> vals)
    : values(::std::move(vals))
{
}

ASTExprNodeArray::ASTExprNodeArray(ASTExprNodeP val, ASTExprNodeP size)
    : mSize(::std::move(size))
{
    values.push_back(::std::move(val));
}

ASTExprNodeTuple::ASTExprNodeTuple(::std::vector<ASTExprNodeP> vals)
    : values(::std::move(vals))
{
}

ASTExprNodeNamedValue::ASTExprNodeNamedValue(ASTPath path)
    : mPath(::std::move(path))
{
}

ASTExprNodeField::ASTExprNodeField(ASTExprNodeP obj, RcString name)
    : obj(::std::move(obj))
    , mName(::std::move(name))
{
}

ASTExprNodeIndex::ASTExprNodeIndex(ASTExprNodeP obj, ASTExprNodeP idx)
    : obj(::std::move(obj))
    , idx(::std::move(idx))
{
}

ASTExprNodeDeref::ASTExprNodeDeref(ASTExprNodeP value)
    : mValue(::std::move(value))
{
}

ASTExprNodeCast::ASTExprNodeCast(ASTExprNodeP value, ASTType*&& dstType)
    : mValue(::std::move(value))
    , mType(::std::move(dstType))
{
}

ASTExprNodeTypeAnnotation::ASTExprNodeTypeAnnotation(ASTExprNodeP value, ASTType*&& dstType)
    : mValue(::std::move(value))
    , mType(::std::move(dstType))
{
}

ASTExprNodeBinOp::ASTExprNodeBinOp(Type type, ASTExprNodeP left, ASTExprNodeP right)
    : mType(type)
    , left(::std::move(left))
    , right(::std::move(right))
{
}

ASTExprNodeUniOp::ASTExprNodeUniOp(Type type, ASTExprNodeP value)
    : mType(type)
    , mValue(::std::move(value))
{
}

ASTExprNodeMacroDefinition::ASTExprNodeMacroDefinition(unsigned int definitionId, Ident::Hygiene tokenHygiene, Ident::Hygiene definitionHygiene)
    : definitionId(definitionId)
    , tokenHygiene(::std::move(tokenHygiene))
    , definitionHygiene(::std::move(definitionHygiene))
{
}

void ASTNodeVisitor::visit(ASTExprNodeP& cnode) {
    if (cnode.get()) {
        cnode->visit(*this);
    }
}

void ASTNodeVisitorDef::visit(ASTExprNodeP& cnode) {
    if (cnode.isValid()) {
        TRACE_FUNCTION_F(cnode.typeName());
        cnode->visit(*this);
    }
}

bool ASTNodeVisitor::isConst() const {
    return false;
}
