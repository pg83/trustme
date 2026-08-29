#include "ast_expr.h"

#include "output.h"
#include "ast_ast.h"

#include <cctype>

using namespace stl;

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

unsigned int ASTExprNodeSuffixedLiteral::nodeKind() const {
    return ASTExprNodeSuffixedLiteral::kind;
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

#define NODE(nodeType, _print, _clone)          \
    void nodeType ::visit(ASTNodeVisitor& nv) { \
        nv.visit(*this);                        \
    }                                           \
    void nodeType ::print(ZeroCopyOutput& os) const _print ASTExprNodeP nodeType ::clone() const _clone
#define OPT_CLONE(node) (node.get() ? node->clone() : ASTExprNodeP())

namespace {
    static inline ASTExprNodeP mkExprnodep(const Span& pos, ASTExprNodeP en) {
        en->setSpan(pos);
        return en;
    }

    bool macroTokenNeedsSpace(eTokenType previous, eTokenType current) {
        switch (current) {
            case TOK_PAREN_CLOSE:
            case TOK_SQUARE_CLOSE:
            case TOK_BRACE_CLOSE:
            case TOK_COMMA:
            case TOK_SEMICOLON:
            case TOK_COLON:
            case TOK_DOUBLE_COLON:
            case TOK_DOT:
            case TOK_EXCLAM:
                return false;
            default:
                break;
        }
        switch (previous) {
            case TOK_PAREN_OPEN:
            case TOK_SQUARE_OPEN:
            case TOK_DOUBLE_COLON:
            case TOK_DOT:
            case TOK_EXCLAM:
            case TOK_AMP:
                return false;
            default:
                break;
        }
        if ((current == TOK_PAREN_OPEN || current == TOK_SQUARE_OPEN) && previous == TOK_IDENT) {
            return false;
        }
        return true;
    }

    void printMacroTokens(ZeroCopyOutput& os, const TokenTree& tree, bool& hasPrevious, eTokenType& previous) {
        if (tree.isToken()) {
            const auto current = tree.tok().type();
            if (hasPrevious && macroTokenNeedsSpace(previous, current)) {
                os << StringView(" ");
            }
            os << tree.tok().toStr();
            previous = current;
            hasPrevious = true;
            return;
        }
        for (size_t i = 0; i < tree.size(); i++) {
            printMacroTokens(os, tree[i], hasPrevious, previous);
        }
    }

#define NEWNODE(type, ...) mkExprnodep(span(), makeAstExprNode<type>(pool() __VA_OPT__(, ) __VA_ARGS__))

    void fmtIfletConditions(ZeroCopyOutput& os, const std::vector<ASTIfLetCondition>& conditions) {
        for (const auto& cond : conditions) {
            if (&cond != &conditions.front()) {
                os << StringView(" && ");
            }
            if (cond.optPat) {
                os << StringView("let ");
                os << *cond.optPat << StringView(" = ");
            }
            os << StringView("(") << *cond.value << StringView(")");
        }
    }

    std::vector<ASTIfLetCondition> cloneIfletConditions(const std::vector<ASTIfLetCondition>& conditions) {
        std::vector<ASTIfLetCondition> newConds;
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

    static void printClosureParameterPattern(ZeroCopyOutput& os, const ASTPattern& pattern) {
        if (pattern.bindings().empty() && pattern.data().is_MaybeBind()) {
            const auto& ident = pattern.data().as_MaybeBind().name;
            if (ident.isRaw) {
                os << StringView("r#");
            }
            os << ident.name;
            return;
        }
        os << pattern;
    }
}

NODE(
    ASTExprNodeBlock,
    {
        os << StringView("{");
        for (const auto& n : nodes) {
            os << StringView(" ") << *n.node << (n.hasSemicolon ? ";" : "");
        }
        os << StringView(" }");
    },
    {
        std::vector<Line> newNodes;
        for (const auto& n : nodes) {
            newNodes.push_back({n.hasSemicolon, n.node->clone()});
        }
        return NEWNODE(ASTExprNodeBlock, blockType, mv$(newNodes), localMod);
    }
)

NODE(ASTExprNodeAsyncBlock, { os << StringView("async ") << (isMove ? "move " : "") << (isUse ? "use " : "") << *inner; }, { return NEWNODE(ASTExprNodeAsyncBlock, inner->clone(), isMove, isUse); })
NODE(ASTExprNodeGeneratorBlock, { os << (isAsync ? "async gen " : "gen ") << (isMove ? "move " : "") << *inner; }, { return NEWNODE(ASTExprNodeGeneratorBlock, inner->clone(), returnType->clone(), isMove, isCoroutineClosureBody, isAsync); })
NODE(ASTExprNodeTry, { os << StringView("try ") << *inner; }, { return NEWNODE(ASTExprNodeTry, inner->clone()); })

NODE(
    ASTExprNodeMacro,
    {
        path.printPretty(os, false);
        os << StringView("!");
        if (ident.size() > 0) {
            os << StringView(" ") << ident << StringView(" ");
        }
        os << (isBraced ? "{ " : "(");
        bool hasPrevious = false;
        eTokenType previous = TOK_NULL;
        printMacroTokens(os, tokens, hasPrevious, previous);
        os << (isBraced ? " }" : ")");
    },
    { return NEWNODE(ASTExprNodeMacro, ASTPath(path), ident, tokens.clone(), isBraced, definitionHygiene); }
)

NODE(
    ASTExprNodeAsm,
    {
        os << StringView("llvm_asm!( \"") << text << StringView("\"");
        os << StringView(" :");
        for (const auto& v : output) {
            os << StringView(" \"") << v.name << StringView("\" (") << *v.value << StringView("),");
        }
        os << StringView(" :");
        for (const auto& v : input) {
            os << StringView(" \"") << v.name << StringView("\" (") << *v.value << StringView("),");
        }
        os << StringView(" :");
        for (const auto& v : clobbers) {
            os << StringView(" \"") << v << StringView("\",");
        }
        os << StringView(" :");
        for (const auto& v : flags) {
            os << StringView(" \"") << v << StringView("\",");
        }
        os << StringView(" )");
    },
    {
        std::vector<ASTExprNodeAsm::ValRef> outputs;
        for (const auto& v : output) {
            outputs.push_back(ASTExprNodeAsm::ValRef{v.name, v.value->clone()});
        }
        std::vector<ASTExprNodeAsm::ValRef> inputs;
        for (const auto& v : input) {
            inputs.push_back(ASTExprNodeAsm::ValRef{v.name, v.value->clone()});
        }
        return NEWNODE(ASTExprNodeAsm, text, mv$(outputs), mv$(inputs), clobbers, flags);
    }
)

NODE(
    ASTExprNodeAsm2,
    {
        os << StringView("asm!( ");
        for (const auto& l : lines) {
            l.fmt(os);
            os << StringView(", ");
        }
        for (const auto& p : params) {
            switch (p.tag()) {
                case ASTAsmParam::TAG_Const: {
                    auto& e = p.as_Const();
                    os << StringView("const ") << *e;
                    break;
                }
                case ASTAsmParam::TAG_Sym: {
                    auto& e = p.as_Sym();
                    os << StringView("sym ") << e;
                    break;
                }
                case ASTAsmParam::TAG_Label: {
                    auto& e = p.as_Label();
                    os << StringView("label ") << *e.code;
                    break;
                }
                case ASTAsmParam::TAG_RegSingle: {
                    auto& e = p.as_RegSingle();
                    os << StringView("reg(") << e.dir << StringView(" ") << e.spec << StringView(") ") << *e.val;
                    break;
                }
                case ASTAsmParam::TAG_Reg: {
                    auto& e = p.as_Reg();
                    os << StringView("reg(") << e.dir << StringView(" ") << e.spec << StringView(") ");
                    if (e.valIn) {
                        os << *e.valIn;
                    } else {
                        os << StringView("_");
                    }
                    os << StringView(" => ");
                    if (e.valOut) {
                        os << *e.valOut;
                    } else {
                        os << StringView("_");
                    }
                    break;
                }
            }
            os << StringView(", ");
        }
        os << StringView(" )");
    },
    {
        std::vector<Param> params;

        for (const auto& p : this->params) {
            switch (p.tag()) {
                case ASTAsmParam::TAG_Const: {
                    auto& e = p.as_Const();
                    params.push_back(Param::make_Const(e->clone()));
                    break;
                }
                case ASTAsmParam::TAG_Sym: {
                    auto& e = p.as_Sym();
                    params.push_back(Param::make_Sym(e));
                    break;
                }
                case ASTAsmParam::TAG_Label: {
                    auto& e = p.as_Label();
                    params.push_back(Param::make_Label({e.code->clone()}));
                    break;
                }
                case ASTAsmParam::TAG_RegSingle: {
                    auto& e = p.as_RegSingle();
                    params.push_back(Param::make_RegSingle({e.dir, e.spec.clone(), e.val->clone()}));
                    break;
                }
                case ASTAsmParam::TAG_Reg: {
                    auto& e = p.as_Reg();
                    params.push_back(Param::make_Reg({e.dir, e.spec.clone(), e.valIn ? e.valIn->clone() : nullptr, e.valOut ? e.valOut->clone() : nullptr}));
                    break;
                }
            }
        }

        return NEWNODE(ASTExprNodeAsm2, options, lines, std::move(params));
    }
)

NODE(
    ASTExprNodeFlow,
    {
        switch (type) {
            case RETURN:
                os << StringView("return");
                break;
            case TAILCALL:
                os << StringView("become");
                break;
            case YIELD:
                os << StringView("yield");
                break;
            case BREAK:
                os << StringView("break");
                break;
            case CONTINUE:
                os << StringView("continue");
                break;
            case YEET:
                os << StringView("do yeet");
                break;
        }
        if (value) {
            os << StringView(" ") << *value;
        }
    },
    { return NEWNODE(ASTExprNodeFlow, type, target, value ? value->clone() : nullptr); }
)

NODE(
    ASTExprNodeLetBinding,
    {
        os << (isSuper ? "super let " : "let ") << pat << StringView(": ") << type;
        if (value) {
            os << StringView(" = ") << *value;
            if (elseNode) {
                os << StringView(" else ") << *elseNode;
            }
        }
    },
    { return NEWNODE(ASTExprNodeLetBinding, pat.clone(), type->clone(), OPT_CLONE(value), OPT_CLONE(elseNode), isSuper); }
)

NODE(
    ASTExprNodeAssign,
    {
        os << *slot << StringView(" ");
        switch (op) {
            case NONE:
                os << StringView("=");
                break;
            case ADD:
                os << StringView("+=");
                break;
            case SUB:
                os << StringView("-=");
                break;
            case MUL:
                os << StringView("*=");
                break;
            case DIV:
                os << StringView("/=");
                break;
            case MOD:
                os << StringView("%=");
                break;
            case AND:
                os << StringView("&=");
                break;
            case OR:
                os << StringView("|=");
                break;
            case XOR:
                os << StringView("^=");
                break;
            case SHR:
                os << StringView(">>=");
                break;
            case SHL:
                os << StringView("<<=");
                break;
        }
        os << StringView(" ") << *value;
    },
    { return NEWNODE(ASTExprNodeAssign, op, slot->clone(), value->clone()); }
)

NODE(
    ASTExprNodeCallPath,
    {
        path.printPretty(os, false);
        os << StringView("(");
        for (const auto& a : args) {
            if (&a != &args.front()) {
                os << StringView(", ");
            }
            os << *a;
        }
        os << StringView(")");
    },
    {
        std::vector<ASTExprNodeP> args;
        for (const auto& a : this->args) {
            args.push_back(a->clone());
        }
        return NEWNODE(ASTExprNodeCallPath, ASTPath(path), mv$(args));
    }
)

NODE(
    ASTExprNodeCallMethod,
    {
        os << StringView("(") << *val << StringView(").") << method << StringView("(");
        for (const auto& a : args) {
            os << *a << StringView(",");
        }
        os << StringView(")");
    },
    {
        std::vector<ASTExprNodeP> args;
        for (const auto& a : this->args) {
            args.push_back(a->clone());
        }
        return NEWNODE(ASTExprNodeCallMethod, val->clone(), method, mv$(args));
    }
)

NODE(
    ASTExprNodeCallObject,
    {
        os << StringView("(") << *val << StringView(")(");
        for (const auto& a : args) {
            os << *a << StringView(",");
        }
        os << StringView(")");
    },
    {
        std::vector<ASTExprNodeP> args;
        for (const auto& a : this->args) {
            args.push_back(a->clone());
        }
        return NEWNODE(ASTExprNodeCallObject, val->clone(), mv$(args));
    }
)

NODE(ASTExprNodeLoop, { os << StringView("LOOP [") << label << StringView("] ") << *code; }, { return NEWNODE(ASTExprNodeLoop, label, code->clone()); })

NODE(
    ASTExprNodeFor,
    {
        os << StringView("FOR [") << label << StringView("] ") << pattern << StringView("in") << *value;
        os << StringView(" ") << *code;
    },
    { return NEWNODE(ASTExprNodeFor, label, pattern.clone(), value->clone(), code->clone(), isAwait); }
)

NODE(
    ASTExprNodeWhile,
    {
        if (label != "") {
            os << StringView("'") << label << StringView(": ");
        }
        os << StringView("while ");
        fmtIfletConditions(os, conditions);
        os << StringView(" { ") << *code << StringView(" }");
    },
    {
        auto newConds = cloneIfletConditions(conditions);
        return NEWNODE(ASTExprNodeWhile, label, mv$(newConds), code->clone());
    }
)

NODE(
    ASTExprNodeMatch,
    {
        os << StringView("match (") << *val << StringView(") {");
        for (const auto& arm : arms) {
            for (const auto& pat : arm.patterns) {
                os << StringView(" ") << pat;
            }
            if (arm.guard.size() > 0) {
                os << StringView(" if ");
                fmtIfletConditions(os, arm.guard);
            }

            os << StringView(" => ") << *arm.code << StringView(",");
        }
        os << StringView("}");
    },
    {
        std::vector<ASTExprNodeMatchArm> newArms;
        for (const auto& arm : arms) {
            std::vector<ASTPattern> patterns;
            for (const auto& pat : arm.patterns) {
                patterns.push_back(pat.clone());
            }
            newArms.push_back(ASTExprNodeMatchArm(mv$(patterns), cloneIfletConditions(arm.guard), arm.code->clone()));
            newArms.back().attrs = arm.attrs.clone();
        }
        return NEWNODE(ASTExprNodeMatch, val->clone(), mv$(newArms));
    }
)

NODE(
    ASTExprNodeIf,
    {
        for (const auto& arm : arms) {
            if (&arm != arms.data()) {
                os << StringView(" else ");
            }
            os << StringView("if ");
            fmtIfletConditions(os, arm.conditions);
            os << StringView(" { ") << *arm.body << StringView(" }");
        }
        if (elseNode) {
            os << StringView(" else { ") << *elseNode << StringView(" }");
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

NODE(ASTExprNodeWildcardPattern, { os << StringView("_"); }, { return NEWNODE(ASTExprNodeWildcardPattern); })
NODE(
    ASTExprNodeInteger,
    {
        if (datatype == CORETYPE_CHAR) {
            os << StringView("'\\u{") << formatHex(value) << StringView("}'");
        } else {
            os << value;
            if (datatype == CORETYPE_ANY)
                ;
            else {
                os << coretypeName(datatype);
            }
        }
    },
    { return NEWNODE(ASTExprNodeInteger, value, datatype); }
)
NODE(
    ASTExprNodeFloat,
    {
        os << formatFloatValueForToken(value);
        if (datatype != CORETYPE_ANY) {
            os << coretypeName(datatype);
        }
    },
    { return NEWNODE(ASTExprNodeFloat, value, datatype); }
)
NODE(ASTExprNodeBool, { os << value; }, { return NEWNODE(ASTExprNodeBool, value); })
NODE(ASTExprNodeString, { printEscapedLiteral(os, TOK_STRING, reinterpret_cast<const u8*>(value.data()), value.size()); }, { return NEWNODE(ASTExprNodeString, value, hygiene); })
NODE(ASTExprNodeByteString, { printEscapedLiteral(os, TOK_BYTESTRING, reinterpret_cast<const u8*>(value.data()), value.size()); }, { return NEWNODE(ASTExprNodeByteString, value); })
NODE(ASTExprNodeCString, { printEscapedLiteral(os, TOK_CSTRING, reinterpret_cast<const u8*>(value.data()), value.size()); }, { return NEWNODE(ASTExprNodeCString, value); })
NODE(ASTExprNodeSuffixedLiteral, { os << text; }, { return NEWNODE(ASTExprNodeSuffixedLiteral, text); })

NODE(
    ASTExprNodeClosure,
    {
        os << hrbs;
        if (isPinned) {
            os << StringView("static ");
        }
        if (isMove) {
            os << StringView("move ");
        }
        if (isUse) {
            os << StringView("use ");
        }
        os << StringView("|");
        bool needsComma = false;
        for (const auto& a : args) {
            if (needsComma) {
                os << StringView(", ");
            }
            needsComma = true;
            printClosureParameterPattern(os, a.first);
            if (!a.second->isWildcard()) {
                os << StringView(": ");
                a.second->print(os, false);
            }
        }
        os << StringView("|");
        if (!returnType->isWildcard()) {
            os << StringView(" -> ");
            returnType->print(os, false);
        }
        os << StringView(" ") << *code;
    },
    {
        ASTExprNodeClosure::argsT args;
        for (const auto& a : this->args) {
            args.push_back(std::make_pair(a.first.clone(), a.second->clone()));
        }
        return NEWNODE(ASTExprNodeClosure, mv$(args), returnType->clone(), code->clone(), isMove, isUse, isPinned, trackCaller);
    }
);

NODE(
    ASTExprNodeStructLiteral,
    {
        path.printPretty(os, false);
        os << StringView(" { ");
        for (const auto& v : values) {
            if (&v != &values.front()) {
                os << StringView(", ");
            }
            os << v.name << StringView(": ") << *v.value;
        }
        if (baseValue) {
            if (!values.empty()) {
                os << StringView(", ");
            }
            os << StringView("..") << *baseValue;
        }
        os << StringView(" }");
    },
    {
        ASTExprNodeStructLiteral::tValues vals;

        for (const auto& v : values) {
            vals.push_back({v.attrs.clone(), v.name, v.value->clone()});
        }

        return NEWNODE(ASTExprNodeStructLiteral, ASTPath(path), OPT_CLONE(baseValue), mv$(vals));
    }
)
NODE(
    ASTExprNodeStructLiteralPattern,
    {
        os << path << StringView(" /*pat*/ { ");
        for (const auto& v : values) {
            os << v.name << StringView(": ") << *v.value << StringView(", ");
        }
        os << StringView(".. }");
    },
    {
        ASTExprNodeStructLiteral::tValues vals;

        for (const auto& v : values) {
            vals.push_back({v.attrs.clone(), v.name, v.value->clone()});
        }

        return NEWNODE(ASTExprNodeStructLiteralPattern, ASTPath(path), mv$(vals));
    }
)

NODE(
    ASTExprNodeArray,
    {
        os << StringView("[");
        if (size.get()) {
            os << *values[0] << StringView("; ") << *size;
        } else {
            for (const auto& a : values) {
                os << *a << StringView(",");
            }
        }
        os << StringView("]");
    },
    {
        if (size.get()) {
            return NEWNODE(ASTExprNodeArray, values[0]->clone(), size->clone());
        } else {
            std::vector<ASTExprNodeP> nodes;
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
        os << StringView("(");
        for (const auto& a : values) {
            os << *a << StringView(",");
        }
        os << StringView(")");
    },
    {
        std::vector<ASTExprNodeP> nodes;
        for (const auto& n : values) {
            nodes.push_back(n->clone());
        }
        return NEWNODE(ASTExprNodeTuple, mv$(nodes));
    }
)

NODE(ASTExprNodeNamedValue, { path.printPretty(os, false); }, { return NEWNODE(ASTExprNodeNamedValue, ASTPath(path)); })

NODE(ASTExprNodeField, { os << StringView("(") << *obj << StringView(").") << name; }, { return NEWNODE(ASTExprNodeField, obj->clone(), name); })

NODE(ASTExprNodeIndex, { os << StringView("(") << *obj << StringView(")[") << *idx << StringView("]"); }, { return NEWNODE(ASTExprNodeIndex, obj->clone(), idx->clone()); })

NODE(ASTExprNodeDeref, { os << StringView("*(") << *value << StringView(")"); }, { return NEWNODE(ASTExprNodeDeref, value->clone()); });

NODE(ASTExprNodeCast, { os << StringView("(") << *value << StringView(" as ") << type << StringView(")"); }, { return NEWNODE(ASTExprNodeCast, value->clone(), type->clone()); })
NODE(ASTExprNodeTypeAnnotation, { os << StringView("(") << *value << StringView(": ") << type << StringView(")"); }, { return NEWNODE(ASTExprNodeTypeAnnotation, value->clone(), type->clone()); })

NODE(
    ASTExprNodeBinOp,
    {
        if (type == RANGE_INC) {
            os << StringView("(");
            if (left) {
                os << *left << StringView(" ");
            }
            os << StringView("... ") << *right;
            os << StringView(")");
            return;
        }
        if (type == RANGE) {
            os << StringView("(");
            if (left) {
                os << *left;
            }
            os << StringView("..");
            if (right) {
                os << StringView(" ") << *right;
            }
            os << StringView(")");
            return;
        }
        os << StringView("(") << *left << StringView(" ");
        switch (type) {
            case CMPEQU:
                os << StringView("==");
                break;
            case CMPNEQU:
                os << StringView("!=");
                break;
            case CMPLT:
                os << StringView("<");
                break;
            case CMPLTE:
                os << StringView("<=");
                break;
            case CMPGT:
                os << StringView(">");
                break;
            case CMPGTE:
                os << StringView(">=");
                break;
            case BOOLAND:
                os << StringView("&&");
                break;
            case BOOLOR:
                os << StringView("||");
                break;
            case BITAND:
                os << StringView("&");
                break;
            case BITOR:
                os << StringView("|");
                break;
            case BITXOR:
                os << StringView("^");
                break;
            case SHR:
                os << StringView(">>");
                break;
            case SHL:
                os << StringView("<<");
                break;
            case MULTIPLY:
                os << StringView("*");
                break;
            case DIVIDE:
                os << StringView("/");
                break;
            case MODULO:
                os << StringView("%");
                break;
            case ADD:
                os << StringView("+");
                break;
            case SUB:
                os << StringView("-");
                break;
            case RANGE:
                os << StringView("..");
                break;
            case RANGE_INC:
                os << StringView("...");
                break;
            case PLACE_IN:
                os << StringView("<-");
                break;
        }
        os << StringView(" ") << *right << StringView(")");
    },
    {
        auto rv = NEWNODE(ASTExprNodeBinOp, type, OPT_CLONE(left), OPT_CLONE(right));
        static_cast<ASTExprNodeBinOp&>(*rv).parenthesised = parenthesised;
        return rv;
    }
)

NODE(
    ASTExprNodeUniOp,
    {
        switch (type) {
            case NEGATE:
                os << StringView("-");
                break;
            case INVERT:
                os << StringView("!");
                break;
            case BOX:
                os << StringView("box ");
                break;
            case REF:
                os << StringView("&");
                break;
            case REFMUT:
                os << StringView("&mut ");
                break;
            case RawBorrow:
                os << StringView("&raw const ");
                break;
            case RawBorrowMut:
                os << StringView("&raw mut ");
                break;
            case PinBorrow:
                os << StringView("&pin const ");
                break;
            case PinBorrowMut:
                os << StringView("&pin mut ");
                break;
            case QMARK:
                os << *value << StringView("?");
                return;
            case AWait:
                os << *value << StringView(".await");
                return;
            case AWaitNext:
                os << *value << StringView(".await/*next*/");
                return;
            case USE:
                os << *value << StringView(".use");
                return;
        }
        os << *value;
    },
    { return NEWNODE(ASTExprNodeUniOp, type, value->clone()); }
)

NODE(ASTExprNodeMacroDefinition, { os << StringView("/* macro definition #") << definitionId << StringView(" */"); }, { return NEWNODE(ASTExprNodeMacroDefinition, definitionId, tokenHygiene, definitionHygiene); })

#define NV(type, actions)                       \
    void ASTNodeVisitorDef::visit(type& node) { \
        actions                                 \
    }

NV(ASTExprNodeBlock, {
    for (auto& child : node.nodes) {
        visit(child.node);
    }
})
NV(ASTExprNodeAsyncBlock, { visit(node.inner); })
NV(ASTExprNodeGeneratorBlock, { visit(node.inner); })
NV(ASTExprNodeTry, { visit(node.inner); })
NV(ASTExprNodeMacro, { BUG(node.span(), StringView("Hit unexpanded macro in expression - ") << node); })
NV(ASTExprNodeAsm, {
    for (auto& v : node.output) {
        visit(v.value);
    }
    for (auto& v : node.input) {
        visit(v.value);
    }
})
NV(ASTExprNodeAsm2, {
    for (auto& v : node.params) {
        switch (v.tag()) {
            case ASTAsmParam::TAG_Const: {
                auto& e = v.as_Const();
                visit(e);
                break;
            }
            case ASTAsmParam::TAG_Sym: {
                break;
            }
            case ASTAsmParam::TAG_Label: {
                auto& e = v.as_Label();
                visit(e.code);
                break;
            }
            case ASTAsmParam::TAG_RegSingle: {
                auto& e = v.as_RegSingle();
                visit(e.val);
                break;
            }
            case ASTAsmParam::TAG_Reg: {
                auto& e = v.as_Reg();
                visit(e.valIn);
                visit(e.valOut);
                break;
            }
        }
    }
})
NV(ASTExprNodeFlow, { visit(node.value); })
NV(ASTExprNodeLetBinding, {
    // TODO: Handle recurse into Let pattern?
    visit(node.value);
    visit(node.elseNode);
})
NV(ASTExprNodeAssign, {
    visit(node.slot);
    visit(node.value);
})
NV(ASTExprNodeCallPath, {
    for (auto& arg : node.args) {
        visit(arg);
    }
})
NV(ASTExprNodeCallMethod, {
    visit(node.val);
    for (auto& arg : node.args) {
        visit(arg);
    }
})
NV(ASTExprNodeCallObject, {
    visit(node.val);
    for (auto& arg : node.args) {
        visit(arg);
    }
})
NV(ASTExprNodeLoop, { visit(node.code); })
NV(ASTExprNodeFor, {
    visit(node.value);
    visit(node.code);
})
NV(ASTExprNodeWhile, {
    for (auto& c : node.conditions) {
        visit(c.value);
    }
    visit(node.code);
})
NV(ASTExprNodeMatch, {
    visit(node.val);
    for (auto& arm : node.arms) {
        for (auto& c : arm.guard) {
            visit(c.value);
        }
        visit(arm.code);
    }
})
NV(ASTExprNodeIf, {
    for (auto& a : node.arms) {
        for (auto& c : a.conditions) {
            visit(c.value);
        }
        visit(a.body);
    }
    visit(node.elseNode);
})

NV(ASTExprNodeWildcardPattern, { (void)node; })
NV(ASTExprNodeInteger, { (void)node; })
NV(ASTExprNodeFloat, { (void)node; })
NV(ASTExprNodeBool, { (void)node; })
NV(ASTExprNodeString, { (void)node; })
NV(ASTExprNodeByteString, { (void)node; })
NV(ASTExprNodeCString, { (void)node; })
NV(ASTExprNodeSuffixedLiteral, { (void)node; })

NV(ASTExprNodeClosure, { visit(node.code); });
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
    visit(node.size);
    for (auto& val : node.values) {
        visit(val);
    }
})
NV(ASTExprNodeTuple, {
    for (auto& val : node.values) {
        visit(val);
    }
})
NV(ASTExprNodeNamedValue, {})

NV(ASTExprNodeField, { visit(node.obj); })
NV(ASTExprNodeIndex, {
    visit(node.obj);
    visit(node.idx);
})
NV(ASTExprNodeDeref, { visit(node.value); })
NV(ASTExprNodeCast, { visit(node.value); })
NV(ASTExprNodeTypeAnnotation, { visit(node.value); })
NV(ASTExprNodeBinOp, {
    visit(node.left);
    visit(node.right);
})
NV(ASTExprNodeUniOp, { visit(node.value); })
NV(ASTExprNodeMacroDefinition, {})
#undef NV

void ASTExprNode::setAttrs(ASTAttributeList&& mi) {
    for (auto& i : mi.items) {
        attrs_.items.push_back(mv$(i));
    }
    mi.items.clear();
}

ASTExprNodeBlock::ASTExprNodeBlock(std::vector<Line> nodes)
    : blockType(Type::Bare)
    , label("")
    , localMod()
    , nodes(std::move(nodes))
{
}

ASTExprNodeBlock::ASTExprNodeBlock(ASTExprNodeP value)
    : ASTExprNodeBlock()
{
    setSpan(value->span());
    nodes.push_back({false, std::move(value)});
}

ASTExprNodeBlock::ASTExprNodeBlock(Type type, std::vector<Line> nodes, std::shared_ptr<ASTModule> localMod)
    : blockType(type)
    , label("")
    , localMod(std::move(localMod))
    , nodes(std::move(nodes))
{
}

ASTExprNodeAsyncBlock::ASTExprNodeAsyncBlock(ASTExprNodeP inner, bool isMove, bool isUse)
    : inner(std::move(inner))
    , isMove(isMove)
    , isUse(isUse)
{
}

ASTExprNodeGeneratorBlock::ASTExprNodeGeneratorBlock(ASTExprNodeP inner, ASTType* returnType, bool isMove, bool isCoroutineClosureBody, bool isAsync)
    : inner(std::move(inner))
    , returnType(returnType)
    , isMove(isMove)
    , isCoroutineClosureBody(isCoroutineClosureBody)
    , isAsync(isAsync)
{
}

ASTExprNodeTry::ASTExprNodeTry(ASTExprNodeP inner)
    : inner(std::move(inner))
{
}

ASTExprNodeMacro::ASTExprNodeMacro(ASTPath name, RcString ident, ::TokenTree&& tokens, bool isBraced, Ident::Hygiene definitionHygiene)
    : path(std::move(name))
    , ident(ident)
    , tokens(std::move(tokens))
    , isBraced(isBraced)
    , definitionHygiene(std::move(definitionHygiene))
{
}

ASTExprNodeAsm::ASTExprNodeAsm(std::string text, std::vector<ValRef> output, std::vector<ValRef> input, std::vector<std::string> clobbers, std::vector<std::string> flags)
    : text(std::move(text))
    , output(std::move(output))
    , input(std::move(input))
    , clobbers(std::move(clobbers))
    , flags(std::move(flags))
{
}

ASTExprNodeAsm2::ASTExprNodeAsm2(AsmOptions options, std::vector<AsmLine> lines, std::vector<Param> params)
    : options(options)
    , lines(std::move(lines))
    , params(std::move(params))
{
}

ASTExprNodeFlow::ASTExprNodeFlow(Type type, Ident target, ASTExprNodeP value)
    : type(type)
    , target(std::move(target))
    , value(std::move(value))
{
}

ASTExprNodeLetBinding::ASTExprNodeLetBinding(ASTPattern pat, ASTType* type, ASTExprNodeP value, ASTExprNodeP elseArm, bool isSuper)
    : pat(std::move(pat))
    , type(std::move(type))
    , value(std::move(value))
    , elseNode(std::move(elseArm))
    , isSuper(isSuper)
{
}

ASTExprNodeAssign::ASTExprNodeAssign()
    : op(NONE)
{
}

ASTExprNodeAssign::ASTExprNodeAssign(Operation op, ASTExprNodeP slot, ASTExprNodeP value)
    : op(op)
    , slot(std::move(slot))
    , value(std::move(value))
{
}

ASTExprNodeCallPath::ASTExprNodeCallPath(ASTPath&& path, std::vector<ASTExprNodeP>&& args)
    : path(std::move(path))
    , args(std::move(args))
{
}

ASTExprNodeCallMethod::ASTExprNodeCallMethod(ASTExprNodeP obj, ASTPathNode method, std::vector<ASTExprNodeP> args)
    : val(std::move(obj))
    , method(std::move(method))
    , args(std::move(args))
{
}

ASTExprNodeCallObject::ASTExprNodeCallObject(ASTExprNodeP val, std::vector<ASTExprNodeP>&& args)
    : val(std::move(val))
    , args(std::move(args))
{
}

ASTExprNodeLoop::ASTExprNodeLoop()
    : label("")
{
}

ASTExprNodeLoop::ASTExprNodeLoop(Ident label, ASTExprNodeP code)
    : label(std::move(label))
    , code(std::move(code))
{
}

ASTExprNodeFor::ASTExprNodeFor(Ident label, ASTPattern pattern, ASTExprNodeP val, ASTExprNodeP code, bool isAwait)
    : label(std::move(label))
    , pattern(std::move(pattern))
    , value(std::move(val))
    , code(std::move(code))
    , isAwait(isAwait)
{
}

ASTExprNodeWhile::ASTExprNodeWhile(Ident label, std::vector<ASTIfLetCondition> conditions, ASTExprNodeP code)
    : label(std::move(label))
    , conditions(std::move(conditions))
    , code(std::move(code))
{
}

ASTExprNodeMatchArm::ASTExprNodeMatchArm() {
}

ASTExprNodeMatchArm::ASTExprNodeMatchArm(std::vector<ASTPattern> patterns, std::vector<ASTIfLetCondition> guard, ASTExprNodeP code)
    : patterns(mv$(patterns))
    , guard(mv$(guard))
    , code(mv$(code))
{
}

ASTExprNodeMatch::ASTExprNodeMatch(ASTExprNodeP val, std::vector<ASTExprNodeMatchArm> arms)
    : val(std::move(val))
    , arms(std::move(arms))
{
}

ASTExprNodeIf::ASTExprNodeIf(std::vector<Arm> arms, ASTExprNodeP elseCode)
    : arms(std::move(arms))
    , elseNode(std::move(elseCode))
{
}

ASTExprNodeInteger::ASTExprNodeInteger(U128 value, enum eCoreType datatype)
    : datatype(datatype)
    , value(value)
{
}

ASTExprNodeFloat::ASTExprNodeFloat(FloatValue value, enum eCoreType datatype)
    : datatype(datatype)
    , value(value)
{
}

ASTExprNodeBool::ASTExprNodeBool(bool value)
    : value(value)
{
}

ASTExprNodeString::ASTExprNodeString(std::string value, Ident::Hygiene h)
    : value(std::move(value))
    , hygiene(std::move(h))
{
}

ASTExprNodeByteString::ASTExprNodeByteString(std::string value)
    : value(std::move(value))
{
}

ASTExprNodeCString::ASTExprNodeCString(std::string value)
    : value(std::move(value))
{
}

ASTExprNodeSuffixedLiteral::ASTExprNodeSuffixedLiteral(std::string text)
    : text(std::move(text))
{
}

ASTExprNodeStructLiteral::ASTExprNodeStructLiteral(ASTPath path, ASTExprNodeP baseValue, tValues&& values)
    : path(std::move(path))
    , baseValue(std::move(baseValue))
    , values(std::move(values))
{
}

ASTExprNodeStructLiteralPattern::ASTExprNodeStructLiteralPattern(ASTPath path, tValues&& values)
    : path(std::move(path))
    , values(std::move(values))
{
}

ASTExprNodeArray::ASTExprNodeArray(std::vector<ASTExprNodeP> vals)
    : values(std::move(vals))
{
}

ASTExprNodeArray::ASTExprNodeArray(ASTExprNodeP val, ASTExprNodeP size)
    : size(std::move(size))
{
    values.push_back(std::move(val));
}

ASTExprNodeTuple::ASTExprNodeTuple(std::vector<ASTExprNodeP> vals)
    : values(std::move(vals))
{
}

ASTExprNodeNamedValue::ASTExprNodeNamedValue(ASTPath path)
    : path(std::move(path))
{
}

ASTExprNodeField::ASTExprNodeField(ASTExprNodeP obj, RcString name)
    : obj(std::move(obj))
    , name(std::move(name))
{
}

ASTExprNodeIndex::ASTExprNodeIndex(ASTExprNodeP obj, ASTExprNodeP idx)
    : obj(std::move(obj))
    , idx(std::move(idx))
{
}

ASTExprNodeDeref::ASTExprNodeDeref(ASTExprNodeP value)
    : value(std::move(value))
{
}

ASTExprNodeCast::ASTExprNodeCast(ASTExprNodeP value, ASTType*&& dstType)
    : value(std::move(value))
    , type(std::move(dstType))
{
}

ASTExprNodeTypeAnnotation::ASTExprNodeTypeAnnotation(ASTExprNodeP value, ASTType*&& dstType)
    : value(std::move(value))
    , type(std::move(dstType))
{
}

ASTExprNodeBinOp::ASTExprNodeBinOp(Type type, ASTExprNodeP left, ASTExprNodeP right)
    : type(type)
    , left(std::move(left))
    , right(std::move(right))
{
}

ASTExprNodeUniOp::ASTExprNodeUniOp(Type type, ASTExprNodeP value)
    : type(type)
    , value(std::move(value))
{
}

ASTExprNodeMacroDefinition::ASTExprNodeMacroDefinition(unsigned int definitionId, Ident::Hygiene tokenHygiene, Ident::Hygiene definitionHygiene)
    : definitionId(definitionId)
    , tokenHygiene(std::move(tokenHygiene))
    , definitionHygiene(std::move(definitionHygiene))
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

namespace stl {
    template <>
    void output<ZeroCopyOutput, ASTExprNodeMacro>(ZeroCopyOutput& os, const ASTExprNodeMacro& node) {
        node.print(os);
    }

    template <>
    void output<ZeroCopyOutput, ASTExprNode>(ZeroCopyOutput& os, const ASTExprNode& node) {
        BUG_ASSERT(static_cast<const void*>(&node) != nullptr);
        node.print(os);
        return;
    }
}
