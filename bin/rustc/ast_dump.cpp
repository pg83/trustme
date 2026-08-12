#include "ast_dump.h"

#include "ast_ast.h"
#include "hir_hir.h" // ABI_RUST - TODO: Move elsewhere?
#include "ast_expr.h"
#include "ast_crate.h"
#include "cpp_unpack.h"

#include <limits> // std::numeric_limits
#include <fstream>
#include <string_view>

#define IS(v, c) (cast<c>(&v) != 0)
#define WRAPIF_CMD(v, t) || IS(v, t)
#define WRAPIF(uniqPtr, class1, ...)                                    \
    do {                                                                \
        auto& _v = *(uniqPtr);                                          \
        if (IS(_v, class1) CC_ITERATE(WRAPIF_CMD, (_v), __VA_ARGS__)) { \
            parenWrap(uniqPtr);                                         \
        } else {                                                        \
            ASTNodeVisitor::visit(uniqPtr);                             \
        }                                                               \
    } while (0)

class RustPrinter: public ASTNodeVisitor {
    ::std::ostream& os;
    int indentLevel;
    bool exprRoot; //!< used to allow 'if' and 'match' to behave differently as standalone exprs
public:
    RustPrinter(::std::ostream& os)
        : os(os)
        , indentLevel(0)
        , exprRoot(false)
    {
    }

    void handleModule(const ASTModule& mod);
    void handleStruct(const ASTStruct& s);
    void handleEnum(const ASTEnum& s);
    void handleTrait(const ASTTrait& s);

    void handleFunction(const ASTVisibility& vis, const RcString& name, const ASTFunction& f);

    virtual bool isConst() const override {
        return true;
    }

    virtual void visit(ASTExprNodeBlock& n) override {
        switch (n.blockType) {
            case ASTExprNodeBlock::Type::Bare:
                break;
            case ASTExprNodeBlock::Type::Unsafe:
                os << "unsafe ";
                break;
            case ASTExprNodeBlock::Type::Const:
                os << "const ";
                break;
        }
        if (n.label.name != RcString()) {
            os << "'" << n.label << ": ";
        }
        os << "{";
        incIndent();
        if (n.localMod) {
            os << "\n";
            os << indent() << "// ANON: " << n.localMod->path() << "\n";
            handleModule(*n.localMod);
        }
        for (auto& child : n.nodes) {
            os << "\n";
            if (child.node) {
                this->printAttrs(child.node->attrs());
            }
            os << indent();
            exprRoot = true;
            if (!child.node.get()) {
                os << "/* nil */";
            } else {
                ASTNodeVisitor::visit(child.node);
            }
            if (child.hasSemicolon) {
                os << ";";
            }
        }
        os << "\n";
        decIndent();
        os << indent() << "}";
    }

    virtual void visit(ASTExprNodeAsyncBlock& n) override {
        os << "async ";
        if (n.isMove) {
            os << "move ";
        }
        ASTNodeVisitor::visit(n.inner);
    }

    virtual void visit(ASTExprNodeGeneratorBlock& n) override {
        os << "gen ";
        if (n.isMove) {
            os << "move ";
        }
        ASTNodeVisitor::visit(n.inner);
    }

    virtual void visit(ASTExprNodeTry& n) override {
        os << "try ";
        ASTNodeVisitor::visit(n.inner);
    }

    void dumpToken(const Token& t) {
        os << t.toStr() << " ";
    }

    void dumpTokentree(const TokenTree& tt) {
        if (tt.isToken()) {
            dumpToken(tt.tok());
        } else {
            for (size_t i = 0; i < tt.size(); i++) {
                dumpTokentree(tt[i]);
            }
        }
    }

    virtual void visit(ASTExprNodeMacro& n) override {
        exprRoot = false;
        os << n.mPath << "!";
        if (n.ident != "") {
            os << " ";
            os << n.ident;
        }
        os << (n.isBraced ? "{" : "(");
        dumpTokentree(n.tokens);
        os << (n.isBraced ? "}" : ")");
    }

    virtual void visit(ASTExprNodeAsm& n) override {
        os << "asm!( \"" << n.text << "\"";
        os << " :";
        for (auto& v : n.output) {
            os << " \"" << v.name << "\" (";
            ASTNodeVisitor::visit(v.value);
            os << "),";
        }
        os << " :";
        for (auto& v : n.input) {
            os << " \"" << v.name << "\" (";
            ASTNodeVisitor::visit(v.value);
            os << "),";
        }
        os << " :";
        for (const auto& v : n.clobbers) {
            os << " \"" << v << "\",";
        }
        os << " :";
        for (const auto& v : n.flags) {
            os << " \"" << v << "\",";
        }
        os << " )";
    }

    virtual void visit(ASTExprNodeAsm2& n) override {
        os << "asm!( ";
        for (const auto& l : n.lines) {
            l.fmt(os);
            os << ", ";
        }
        for (auto& p : n.mParams) {
            TU_MATCH_HDRA((p), {)
            TU_ARMA(Const, e) {
                    os << "const ";
                    ASTNodeVisitor::visit(e);
                }
                TU_ARMA(Sym, e) {
                    os << "sym " << e;
                }
                TU_ARMA(RegSingle, e) {
                    os << e.dir << "(" << e.spec << ") ";
                    ASTNodeVisitor::visit(e.val);
                }
                TU_ARMA(Reg, e) {
                    os << e.dir << "(" << e.spec << ") ";
                    if (e.valIn) {
                        ASTNodeVisitor::visit(e.valIn);
                        if (e.valOut) {
                            os << " => ";
                        }
                    }
                    if (e.valOut) {
                        ASTNodeVisitor::visit(e.valOut);
                    }
                }
            }
            os << ", ";
        }
        if (n.options.any()) {
            n.options.fmt(os);
            //m_os << "options(";
            //m_os << ")";
        }
        os << ")";
    }

    virtual void visit(ASTExprNodeFlow& n) override {
        exprRoot = false;
        switch (n.mType) {
            case ASTExprNodeFlow::RETURN:
                os << "return ";
                break;
            case ASTExprNodeFlow::YIELD:
                os << "yield ";
                break;
            case ASTExprNodeFlow::BREAK:
                os << "break ";
                break;
            case ASTExprNodeFlow::CONTINUE:
                os << "continue ";
                break;
            case ASTExprNodeFlow::YEET:
                os << "do yeet ";
                break;
        }
        if (n.target.name != "") {
            os << "'" << n.target << " ";
        }
        ASTNodeVisitor::visit(n.mValue);
    }

    virtual void visit(ASTExprNodeLetBinding& n) override {
        exprRoot = false;
        os << "let ";
        printPattern(n.pat, false);
        os << ": ";
        printType(n.mType);
        if (n.mValue) {
            os << " = ";
            ASTNodeVisitor::visit(n.mValue);
        }
        if (n.elseNode) {
            os << " else ";
            ASTNodeVisitor::visit(n.elseNode);
        }
        os << ";";
    }

    virtual void visit(ASTExprNodeAssign& n) override {
        exprRoot = false;
        ASTNodeVisitor::visit(n.slot);
        switch (n.op) {
            case ASTExprNodeAssign::NONE:
                os << "  = ";
                break;
            case ASTExprNodeAssign::ADD:
                os << " += ";
                break;
            case ASTExprNodeAssign::SUB:
                os << " -= ";
                break;
            case ASTExprNodeAssign::MUL:
                os << " *= ";
                break;
            case ASTExprNodeAssign::DIV:
                os << " /= ";
                break;
            case ASTExprNodeAssign::MOD:
                os << " %= ";
                break;
            case ASTExprNodeAssign::AND:
                os << " &= ";
                break;
            case ASTExprNodeAssign::OR:
                os << " |= ";
                break;
            case ASTExprNodeAssign::XOR:
                os << " ^= ";
                break;
            case ASTExprNodeAssign::SHR:
                os << " >>= ";
                break;
            case ASTExprNodeAssign::SHL:
                os << " <<= ";
                break;
        }
        ASTNodeVisitor::visit(n.mValue);
    }

    virtual void visit(ASTExprNodeCallPath& n) override {
        exprRoot = false;
        os << n.mPath;
        os << "(";
        bool isFirst = true;
        for (auto& arg : n.mArgs) {
            if (isFirst) {
                isFirst = false;
            } else {
                os << ", ";
            }
            ASTNodeVisitor::visit(arg);
        }
        os << ")";
    }

    virtual void visit(ASTExprNodeCallMethod& n) override {
        exprRoot = false;
        WRAPIF(n.val, ASTExprNodeDeref, ASTExprNodeUniOp, ASTExprNodeCast, ASTExprNodeBinOp, ASTExprNodeAssign, ASTExprNodeMatch, ASTExprNodeIf, ASTExprNodeMatch);
        os << "." << n.method;
        os << "(";
        bool isFirst = true;
        for (auto& arg : n.mArgs) {
            if (isFirst) {
                isFirst = false;
            } else {
                os << ", ";
            }
            ASTNodeVisitor::visit(arg);
        }
        os << ")";
    }

    virtual void visit(ASTExprNodeCallObject& n) override {
        exprRoot = false;
        os << "(";
        ASTNodeVisitor::visit(n.val);
        os << ")(";
        bool isFirst = true;
        for (auto& arg : n.mArgs) {
            if (isFirst) {
                isFirst = false;
            } else {
                os << ", ";
            }
            ASTNodeVisitor::visit(arg);
        }
        os << ")";
    }

    virtual void visit(ASTExprNodeLoop& n) override {
        bool exprRoot = exprRoot;
        exprRoot = false;

        if (n.label.name != "") {
            os << "'" << n.label << ": ";
        }

        os << "loop";

        if (exprRoot) {
            os << "\n";
            os << indent();
        } else {
            os << " ";
        }

        ASTNodeVisitor::visit(n.mCode);
    }

    virtual void visit(ASTExprNodeFor& n) override {
        bool exprRoot = exprRoot;
        exprRoot = false;

        if (n.label.name != "") {
            os << "'" << n.label << ": ";
        }
        os << "for ";
        printPattern(n.pattern, true);
        os << " in ";
        ASTNodeVisitor::visit(n.mValue);

        if (exprRoot) {
            os << "\n";
            os << indent();
        } else {
            os << " ";
        }

        ASTNodeVisitor::visit(n.mCode);
    }

    void visitIfletConditions(std::vector<ASTIfLetCondition>& conds) {
        for (size_t i = 0; i < conds.size(); i++) {
            if (i != 0) {
                os << " && ";
            }
            if (conds[i].optPat) {
                os << "let ";
                printPattern(*conds[i].optPat, true);
                os << " = ";
            }
            os << "(";
            ASTNodeVisitor::visit(conds[i].value);
            os << ")";
        }
    }

    void visit(ASTExprNodeWhile& n) override {
        bool exprRoot = exprRoot;
        exprRoot = false;

        if (n.label.name != "") {
            os << "'" << n.label << ": ";
        }

        os << "while ";
        visitIfletConditions(n.conditions);
        if (exprRoot) {
            os << "\n";
            os << indent();
        } else {
            os << " ";
        }

        ASTNodeVisitor::visit(n.mCode);
    }

    virtual void visit(ASTExprNodeMatch& n) override {
        bool exprRoot = exprRoot;
        exprRoot = false;
        os << "match ";
        ASTNodeVisitor::visit(n.val);

        if (exprRoot) {
            os << "\n";
            os << indent() << "{\n";
        } else {
            os << " {\n";
            incIndent();
        }

        for (auto& arm : n.arms) {
            os << indent();
            bool isFirst = true;
            for (const auto& pat : arm.patterns) {
                if (!isFirst) {
                    os << "|";
                }
                isFirst = false;
                printPattern(pat, true);
            }
            if (!arm.guard.empty()) {
                os << " if ";
                visitIfletConditions(arm.guard);
            }
            os << " => ";
            // Increase indent, but don't print. Causes nested blocks to be indented above the match
            incIndent();
            ASTNodeVisitor::visit(arm.mCode);
            decIndent();
            os << ",\n";
        }

        if (exprRoot) {
            os << indent() << "}";
        } else {
            os << indent() << "}";
            decIndent();
        }
    }

    virtual void visit(ASTExprNodeIf& n) override {
        bool exprRoot = exprRoot;
        exprRoot = false;
        for (auto& arm : n.arms) {
            if (&arm != n.arms.data()) {
                if (exprRoot) {
                    os << indent();
                }
                os << "else ";
            }

            os << "if ";
            visitIfletConditions(arm.conditions);

            bool isBlock = (cast<const ASTExprNodeBlock>(&*arm.body) != nullptr);
            if (!isBlock) {
                os << "{ ";
            }
            ASTNodeVisitor::visit(arm.body);
            if (!isBlock) {
                os << " }";
            }
            if (exprRoot) {
                os << "\n";
            }
        }
        if (n.elseNode) {
            if (exprRoot) {
                os << indent();
            }
            os << "else";
            bool isBlock = (cast<const ASTExprNodeBlock>(&*n.elseNode) != nullptr);
            if (!isBlock) {
                os << "{ ";
            }
            ASTNodeVisitor::visit(n.elseNode);
            if (!isBlock) {
                os << " }";
            }
        }
    }

    virtual void visit(ASTExprNodeClosure& n) override {
        exprRoot = false;
        if (n.isMove) {
            os << "move ";
        }
        os << "|";
        bool isFirst = true;
        for (const auto& arg : n.mArgs) {
            if (!isFirst) {
                os << ", ";
            }
            isFirst = false;
            printPattern(arg.first, false);
            os << ": ";
            printType(arg.second);
        }
        os << "| ->";
        printType(n.returnType);
        os << " { ";
        ASTNodeVisitor::visit(n.mCode);
        os << " }";
    }

    virtual void visit(ASTExprNodeWildcardPattern& n) override {
        os << "_";
    }

    virtual void visit(ASTExprNodeInteger& n) override {
        exprRoot = false;
        switch (n.datatype) {
            case CORETYPE_INVAL:
                os << "0x" << ::std::hex << n.mValue << ::std::dec << "_/*INVAL*/";
                break;
            case CORETYPE_BOOL:
            case CORETYPE_STR:
                os << "0x" << ::std::hex << n.mValue << ::std::dec << "_/*bool/str*/";
                break;
            case CORETYPE_CHAR:
                //if( 0x20 <= n.m_value && n.m_value < 128 ) {
                if (n.mValue >= 0x20 && n.mValue < 128) {
                    switch (n.mValue.truncateU64()) {
                        case '\'':
                            os << "'\\''";
                            break;
                        case '\\':
                            os << "'\\\\'";
                            break;
                        default:
                            os << "'" << (char)n.mValue.truncateU64() << "'";
                            break;
                    }
                } else {
                    os << "'\\u{" << ::std::hex << n.mValue << ::std::dec << "}'";
                }
                break;
            case CORETYPE_F16:
            case CORETYPE_F32:
            case CORETYPE_F64:
            case CORETYPE_F128:
                break;
            case CORETYPE_U8:
            case CORETYPE_U16:
            case CORETYPE_U32:
            case CORETYPE_U64:
            case CORETYPE_U128:
            case CORETYPE_UINT:
            case CORETYPE_ANY:
                os << "0x" << ::std::hex << n.mValue << ::std::dec;
                os << "_" << coretypeName(n.datatype);
                break;
            case CORETYPE_I8:
            case CORETYPE_I16:
            case CORETYPE_I32:
            case CORETYPE_I64:
            case CORETYPE_I128:
            case CORETYPE_INT:
                os << n.mValue;
                os << "_" << coretypeName(n.datatype);
                break;
        }
    }

    virtual void visit(ASTExprNodeFloat& n) override {
        exprRoot = false;
        switch (n.datatype) {
            case CORETYPE_ANY:
                os.precision(::std::numeric_limits<double>::max_digits10 + 1);
                os << n.mValue;
                break;
            case CORETYPE_F16:
            case CORETYPE_F32:
                os.precision(::std::numeric_limits<float>::max_digits10 + 1);
                os << n.mValue;
                os << "_" << coretypeName(n.datatype);
                break;
            case CORETYPE_F64:
                os.precision(::std::numeric_limits<double>::max_digits10 + 1);
                os << n.mValue;
                os << "_" << coretypeName(n.datatype);
                break;
            case CORETYPE_F128:
                os.precision(::std::numeric_limits<double>::max_digits10 + 1);
                os << n.mValue;
                os << "_" << coretypeName(n.datatype);
                break;
            default:
                break;
        }
    }

    virtual void visit(ASTExprNodeBool& n) override {
        exprRoot = false;
        if (n.mValue) {
            os << "true";
        } else {
            os << "false";
        }
    }

    virtual void visit(ASTExprNodeString& n) override {
        exprRoot = false;
        os << "\"" << FmtEscaped(n.mValue) << "\"";
    }

    virtual void visit(ASTExprNodeByteString& n) override {
        exprRoot = false;
        os << "b\"" << FmtEscaped(n.mValue) << "\"";
    }

    virtual void visit(ASTExprNodeCString& n) override {
        exprRoot = false;
        os << "c\"" << FmtEscaped(n.mValue) << "\"";
    }

    virtual void visit(ASTExprNodeStructLiteral& n) override {
        exprRoot = false;
        os << n.mPath << " {\n";
        incIndent();
        for (auto& i : n.values) {
            printAttrs(i.attrs);
            os << indent() << "r#" << i.name << ": ";
            ASTNodeVisitor::visit(i.value);
            os << ",\n";
        }
        if (n.baseValue.get()) {
            os << indent() << ".. ";
            ASTNodeVisitor::visit(n.baseValue);
            os << "\n";
        }
        os << indent() << "}";
        decIndent();
    }

    virtual void visit(ASTExprNodeStructLiteralPattern& n) override {
        exprRoot = false;
        os << n.mPath << " {\n";
        incIndent();
        for (auto& i : n.values) {
            printAttrs(i.attrs);
            os << indent() << "r#" << i.name << ": ";
            ASTNodeVisitor::visit(i.value);
            os << ",\n";
        }
        os << indent() << "..\n";
        os << indent() << "}";
        decIndent();
    }

    virtual void visit(ASTExprNodeArray& n) override {
        exprRoot = false;
        os << "[";
        if (n.mSize.get()) {
            ASTNodeVisitor::visit(n.values[0]);
            os << "; ";
            ASTNodeVisitor::visit(n.mSize);
        } else {
            for (auto& item : n.values) {
                ASTNodeVisitor::visit(item);
                os << ", ";
            }
        }
        os << "]";
    }

    virtual void visit(ASTExprNodeTuple& n) override {
        exprRoot = false;
        os << "(";
        for (auto& item : n.values) {
            ASTNodeVisitor::visit(item);
            os << ", ";
        }
        os << ")";
    }

    virtual void visit(ASTExprNodeNamedValue& n) override {
        exprRoot = false;
        os << n.mPath;
    }

    virtual void visit(ASTExprNodeField& n) override {
        exprRoot = false;
        WRAPIF(n.obj, ASTExprNodeDeref, ASTExprNodeUniOp, ASTExprNodeCast, ASTExprNodeBinOp, ASTExprNodeAssign, ASTExprNodeMatch, ASTExprNodeIf, ASTExprNodeMatch);
        os << "." << n.mName;
    }

    virtual void visit(ASTExprNodeIndex& n) override {
        exprRoot = false;
        WRAPIF(n.obj, ASTExprNodeDeref, ASTExprNodeUniOp, ASTExprNodeCast, ASTExprNodeBinOp, ASTExprNodeAssign, ASTExprNodeMatch, ASTExprNodeIf, ASTExprNodeMatch);
        os << "[";
        ASTNodeVisitor::visit(n.idx);
        os << "]";
    }

    virtual void visit(ASTExprNodeDeref& n) override {
        exprRoot = false;
        os << "*(";
        ASTNodeVisitor::visit(n.mValue);
        os << ")";
    }

    virtual void visit(ASTExprNodeCast& n) override {
        exprRoot = false;
        os << "(";
        ASTNodeVisitor::visit(n.mValue);
        os << ") as " << n.mType;
    }

    virtual void visit(ASTExprNodeTypeAnnotation& n) override {
        exprRoot = false;
        os << "(";
        ASTNodeVisitor::visit(n.mValue);
        os << ") : " << n.mType;
    }

    virtual void visit(ASTExprNodeBinOp& n) override {
        exprRoot = false;
        auto* leftBinop = cast<ASTExprNodeBinOp>(n.left.get());
        if (!n.left) {
            os << "/*null*/";
        } else if (leftBinop && leftBinop->mType == n.mType) {
            ASTNodeVisitor::visit(n.left);
        } else {
            WRAPIF(n.left, ASTExprNodeCast, ASTExprNodeBinOp);
        }
        os << " ";
        switch (n.mType) {
            case ASTExprNodeBinOp::CMPEQU:
                os << "==";
                break;
            case ASTExprNodeBinOp::CMPNEQU:
                os << "!=";
                break;
            case ASTExprNodeBinOp::CMPLT:
                os << "<";
                break;
            case ASTExprNodeBinOp::CMPLTE:
                os << "<=";
                break;
            case ASTExprNodeBinOp::CMPGT:
                os << ">";
                break;
            case ASTExprNodeBinOp::CMPGTE:
                os << ">=";
                break;
            case ASTExprNodeBinOp::BOOLAND:
                os << "&&";
                break;
            case ASTExprNodeBinOp::BOOLOR:
                os << "||";
                break;
            case ASTExprNodeBinOp::BITAND:
                os << "&";
                break;
            case ASTExprNodeBinOp::BITOR:
                os << "|";
                break;
            case ASTExprNodeBinOp::BITXOR:
                os << "^";
                break;
            case ASTExprNodeBinOp::SHL:
                os << "<<";
                break;
            case ASTExprNodeBinOp::SHR:
                os << ">>";
                break;
            case ASTExprNodeBinOp::MULTIPLY:
                os << "*";
                break;
            case ASTExprNodeBinOp::DIVIDE:
                os << "/";
                break;
            case ASTExprNodeBinOp::MODULO:
                os << "%";
                break;
            case ASTExprNodeBinOp::ADD:
                os << "+";
                break;
            case ASTExprNodeBinOp::SUB:
                os << "-";
                break;
            case ASTExprNodeBinOp::RANGE:
                os << "..";
                break;
            case ASTExprNodeBinOp::RANGE_INC:
                os << "...";
                break;
            case ASTExprNodeBinOp::PLACE_IN:
                os << "<-";
                break;
        }
        os << " ";
        auto* rightBinop = cast<ASTExprNodeBinOp>(n.right.get());
        if (!n.right) {
            os << "/*null*/";
        } else if (rightBinop && rightBinop->mType != n.mType) {
            parenWrap(n.right);
        } else {
            ASTNodeVisitor::visit(n.right);
        }
    }

    virtual void visit(ASTExprNodeUniOp& n) override {
        exprRoot = false;
        switch (n.mType) {
            case ASTExprNodeUniOp::NEGATE:
                os << "-";
                break;
            case ASTExprNodeUniOp::INVERT:
                os << "!";
                break;
            case ASTExprNodeUniOp::BOX:
                os << "box ";
                break;
            case ASTExprNodeUniOp::REF:
                os << "&";
                break;
            case ASTExprNodeUniOp::REFMUT:
                os << "&mut ";
                break;
            case ASTExprNodeUniOp::RawBorrow:
                os << "&raw const ";
                break;
            case ASTExprNodeUniOp::RawBorrowMut:
                os << "&raw mut ";
                break;
            case ASTExprNodeUniOp::QMARK:
                break;
            case ASTExprNodeUniOp::AWait:
                break;
        }

        bool wrap = IS(*n.mValue, ASTExprNodeBinOp) || IS(*n.mValue, ASTExprNodeCast);
        if (wrap) {
            os << "(";
        }
        ASTNodeVisitor::visit(n.mValue);
        if (wrap) {
            os << ")";
        }
        switch (n.mType) {
            case ASTExprNodeUniOp::QMARK:
                os << "?";
                break;
            case ASTExprNodeUniOp::AWait:
                os << ".await";
                break;
            default:
                break;
        }
    }

    virtual void visit(ASTExprNodeMacroDefinition& n) override {
        os << "/* macro definition #" << n.definitionId << " */";
    }

private:
    void parenWrap(ASTExprNodeP& node) {
        os << "(";
        ASTNodeVisitor::visit(node);
        os << ")";
    }

    void printAttrs(const ASTAttributeList& attrs);
    void printParams(const ASTGenericParams& params);
    void printBounds(const ASTGenericParams& params);
    void printPatternTuple(const ASTPattern::TuplePat& v, bool isRefutable);
    void printPattern(const ASTPattern& p, bool isRefutable);
    void printType(const TypeRef& t);

    void incIndent();
    RepeatLitStr indent();
    void decIndent();
};

void RustPrinter::printAttrs(const ASTAttributeList& attrs) {
    for (const auto& a : attrs.mItems) {
        os << indent() << "#[" << a << "]\n";
    }
}

void RustPrinter::handleModule(const ASTModule& mod) {
    bool needNl = true;

    for (const auto& ip : mod.mItems) {
        const auto& i = *ip;
        if (!i.data.is_Use()) {
            continue;
        }
        const auto& iData = i.data.as_Use();
        //if(need_nl) {
        //    m_os << "\n";
        //    need_nl = false;
        //}
        if (iData.entries.empty()) {
            continue;
        }
        os << indent() << i.vis << "use ";
        if (iData.entries.size() > 1) {
            os << "{";
        }
        for (const auto& ent : iData.entries) {
            if (&ent != &iData.entries.front()) {
                os << ", ";
            }
            os << ent.path;
            if (ent.name == "") {
                os << "::*";
            } else if (ent.path.nodes().size() > 0 && ent.name != ent.path.nodes().back().name()) {
                os << " as " << ent.name;
            } else {
            }
        }
        if (iData.entries.size() > 1) {
            os << "}";
        }
        os << ";\n";
    }
    needNl = true;

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_Crate()) {
            continue;
        }
        const auto& e = item.data.as_Crate();

        printAttrs(item.attrs);
        os << indent() << "extern crate \"" << e.name << "\" as " << item.name << ";\n";
    }

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_ExternBlock()) {
            continue;
        }
        const auto& e = item.data.as_ExternBlock();

        printAttrs(item.attrs);
        os << indent() << "extern \"" << e.abi() << "\" {}\n";
    }

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_Module()) {
            continue;
        }
        const auto& e = item.data.as_Module();

        os << "\n";
        os << indent() << item.vis << "mod " << item.name << "\n";
        os << indent() << "{\n";
        incIndent();
        handleModule(e);
        decIndent();
        os << indent() << "}\n";
        os << "\n";
    }

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_Type()) {
            continue;
        }
        const auto& e = item.data.as_Type();

        if (needNl) {
            os << "\n";
            needNl = false;
        }
        printAttrs(item.attrs);
        os << indent() << item.vis << "type " << item.name;
        printParams(e.params());
        os << " = " << e.type();
        printBounds(e.params());
        os << ";\n";
    }
    needNl = true;

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_Struct()) {
            continue;
        }
        const auto& e = item.data.as_Struct();

        os << "\n";
        printAttrs(item.attrs);
        os << indent() << item.vis << "struct " << item.name;
        handleStruct(e);
    }

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_Enum()) {
            continue;
        }
        const auto& e = item.data.as_Enum();

        os << "\n";
        printAttrs(item.attrs);
        os << indent() << item.vis << "enum " << item.name;
        handleEnum(e);
    }

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_Trait()) {
            continue;
        }
        const auto& e = item.data.as_Trait();

        os << "\n";
        printAttrs(item.attrs);
        os << indent() << item.vis << "trait " << item.name;
        handleTrait(e);
    }

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_Static()) {
            continue;
        }
        const auto& e = item.data.as_Static();

        if (needNl) {
            os << "\n";
            needNl = false;
        }
        printAttrs(item.attrs);
        os << indent() << item.vis;
        switch (e.sClass()) {
            case ASTStatic::CONST:
                os << "const ";
                break;
            case ASTStatic::STATIC:
                os << "static ";
                break;
            case ASTStatic::MUT:
                os << "static mut ";
                break;
        }
        os << item.name << ": " << e.type() << " = ";
        e.value().visitNodes(*this);
        os << ";\n";
    }

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_Function()) {
            continue;
        }
        const auto& e = item.data.as_Function();

        os << "\n";
        printAttrs(item.attrs);
        handleFunction(item.vis, item.name, e);
    }

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_Impl()) {
            continue;
        }
        const auto& i = item.data.as_Impl();

        os << "\n";
        os << indent() << "impl";
        if (i.def().isConst()) {
            os << " const";
        }
        printParams(i.def().params());
        if (i.def().trait().ent != ASTPath()) {
            os << " " << i.def().trait().ent << " for";
        }
        os << " " << i.def().type() << "\n";

        printBounds(i.def().params());
        os << indent() << "{\n";
        incIndent();
        for (const auto& it : i.items()) {
            TU_MATCH_DEF(
                ASTItem,
                (*it.data),
                (e),
                (throw ::std::runtime_error(FMT("Unexpected item type in impl block - " << it.data->tagStr()));),
                (
                    None,
                    // Ignore, it's been deleted by #[cfg]
                ),
                (
                    MacroInv,
                    // TODO: Dump macro invocations
                ),
                (
                    Static, os << indent(); switch (e.sClass()) {
                        case ASTStatic::CONST:
                            os << "const ";
                            break;
                        case ASTStatic::STATIC:
                            os << "static ";
                            break;
                        case ASTStatic::MUT:
                            os << "static mut ";
                            break;
                    } os << it.name
                         << ": " << e.type() << " = ";
                    e.value().visitNodes(*this);
                    os << ";\n";
                ),
                (Type, os << indent() << "type " << it.name << " = " << e.type() << ";\n";),
                (Function, handleFunction(it.vis, it.name, e);)
            )
        }
        decIndent();
        os << indent() << "}\n";
    }
}

void RustPrinter::printParams(const ASTGenericParams& params) {
    if (!params.mParams.empty()) {
        bool isFirst = true;
        os << "<";
        for (const auto& p : params.mParams) {
            if (!isFirst) {
                os << ", ";
            }
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(None, p) {
                    os << "/*-*/";
                }
                TU_ARMA(Lifetime, p) {
                    os << p;
                }
                TU_ARMA(Type, p) {
                    os << p.attrs();
                    os << p.name();
                    if (!p.getDefault().isWildcard()) {
                        os << " = " << p.getDefault();
                    }
                }
                TU_ARMA(Value, p) {
                    os << p.attrs();
                    os << "const " << p.name() << ": " << p.type();
                }
            }
            isFirst = false;
        }
        os << ">";
    }
}

void RustPrinter::printBounds(const ASTGenericParams& params) {
    if (!params.bounds.empty()) {
        incIndent();
        bool isFirst = true;

        for (const auto& b : params.bounds) {
            if (b.is_None()) {
                os << "/*-*/";
                continue;
            }
            if (!isFirst) {
                os << ",\n";
            } else {
                os << indent() << "where\n";
            }
            isFirst = false;

            os << indent();
            TU_MATCH(ASTGenericBound, (b), (ent), (None, os << "/*-*/";), (Lifetime, os << ent.test << ": " << ent.bound;), (TypeLifetime, os << ent.type << ": " << ent.bound;), (IsTrait, os << ent.outerHrbs << ent.type << ": "; if (ent.constness == ASTBoundConstness::Always) os << "const "; else if (ent.constness == ASTBoundConstness::Maybe) os << "[const] "; os << ent.innerHrbs << ent.trait;), (MaybeTrait, os << ent.type << ": ?" << ent.trait;), (NotTrait, os << ent.type << ": !" << ent.trait;), (Equality, os << ent.type << ": =" << ent.replacement;))
        }
        os << "\n";

        decIndent();
    }
}

void RustPrinter::printPatternTuple(const ASTPattern::TuplePat& v, bool isRefutable) {
    for (const auto& sp : v.start) {
        printPattern(sp, isRefutable);
        os << ", ";
    }
    if (v.hasWildcard) {
        os << ".., ";
        for (const auto& sp : v.end) {
            printPattern(sp, isRefutable);
            os << ", ";
        }
    }
}

void RustPrinter::printPattern(const ASTPattern& p, bool isRefutable) {
    for (const auto& pb : p.bindings()) {
        if (pb.isMutable) {
            os << "mut ";
        }
        switch (pb.mType) {
            case ASTPatternBinding::Type::MOVE:
                break;
            case ASTPatternBinding::Type::REF:
                os << "ref ";
                break;
            case ASTPatternBinding::Type::MUTREF:
                os << "ref mut ";
                break;
        }
        os << pb.mName << "/*" << pb.slot << "*/";
        // If binding is irrefutable, and would be binding against a wildcard, just emit the name
        if (!isRefutable && p.bindings().size() == 1 && p.data().is_Any()) {
            return;
        }
        os << " @ ";
    }
    TU_MATCH(
        ASTPattern::Data,
        (p.data()),
        (v),
        (Any, os << "_";),
        (MaybeBind, os << v.name << " /*?*/";),
        (Macro, os << *v.inv;),
        (Box,
         {
             const auto& v = p.data().as_Box();
             os << "box ";
             printPattern(*v.sub, isRefutable);
         }),
        (Ref,
         {
             const auto& v = p.data().as_Ref();
             if (v.mut) {
                 os << "&mut ";
             } else {
                 os << "& ";
             }
             // Just in case the inner binds as mut
             os << "(";
             printPattern(*v.sub, isRefutable);
             os << ")";
         }),
        (Value, os << v.start; if (!v.end.is_Invalid()) { os << " ..= " << v.end; }),
        (ValueLeftInc, os << v.start << " .. " << v.end;),
        (StructTuple, os << v.path << "("; this->printPatternTuple(v.tupPat, isRefutable); os << ")";),
        (Struct,
         {
             const auto& v = p.data().as_Struct();
             os << v.path << "{";
             for (const auto& sp : v.subPatterns) {
                 os << sp.name << ": ";
                 printPattern(sp.pat, isRefutable);
                 os << ",";
             }
             if (!v.isExhaustive) {
                 os << "..";
             }
             os << "}";
         }),
        (Tuple, os << "("; this->printPatternTuple(v, isRefutable); os << ")";),
        (
            Slice, os << "["; for (const auto& sp : v.subPats) {
                printPattern(sp, isRefutable);
                os << ", ";
            } os << "]";
        ),
        (
            SplitSlice, os << "["; bool needsComma = false; for (const auto& sp : v.leading) {
                printPattern(sp, isRefutable);
                os << ", ";
            }

                                                            if (v.extraBind.isValid()) {
                                                                const auto& b = v.extraBind;
                                                                if (b.isMutable) {
                                                                    os << "mut ";
                                                                }
                                                                switch (b.mType) {
                                                                    case ASTPatternBinding::Type::MOVE:
                                                                        break;
                                                                    case ASTPatternBinding::Type::REF:
                                                                        os << "ref ";
                                                                        break;
                                                                    case ASTPatternBinding::Type::MUTREF:
                                                                        os << "ref mut ";
                                                                        break;
                                                                }
                                                                os << b.mName << "/*" << b.slot << "*/";
                                                            } os
                                                            << "..";
            needsComma = true;

            if (v.trailing.size()) {
                if (needsComma) {
                    os << ", ";
                }
                for (const auto& sp : v.trailing) {
                    printPattern(sp, isRefutable);
                    os << ", ";
                }
            } os
            << "]";
        ),
        (Or, os << "("; for (const auto& e : v) {
            os << (&e == &v.front() ? "" : " | ");
            printPattern(e, isRefutable);
        } os << ")";)
    )
}

void RustPrinter::printType(const TypeRef& t) {
    os << t;
}

void RustPrinter::handleStruct(const ASTStruct& s) {
    printParams(s.params());

    TU_MATCH(
        ASTStructData,
        (s.mData),
        (e),
        (Unit, os << " /* unit-like */\n"; printBounds(s.params()); os << indent() << ";\n";),
        (Tuple, os << "("; for (const auto& i : e.ents) { os << i.vis << i.mType << ", "; } os << ")\n"; printBounds(s.params()); os << indent() << ";\n";),
        (Struct, os << "\n"; printBounds(s.params());

         os << indent() << "{\n";
         incIndent();
         for (const auto& i : e.ents) { os << indent() << i.vis << i.mName << ": " << i.mType.printPretty() << ",\n"; } decIndent();
         os << indent() << "}\n";)
    )
    os << "\n";
}

void RustPrinter::handleEnum(const ASTEnum& s) {
    printParams(s.params());
    os << "\n";
    printBounds(s.params());

    os << indent() << "{\n";
    incIndent();
    unsigned int idx = 0;
    for (const auto& i : s.variants()) {
        os << indent() << "/*" << idx << "*/" << i.mName;
        TU_MATCH(ASTEnumVariantData, (i.mData), (e), (Unit, ), (Tuple, os << "("; for (const auto& t : e.mItems) os << t.mType.printPretty() << ", "; os << ")";), (Struct, os << "{\n"; incIndent(); for (const auto& i : e.fields) { os << indent() << i.mName << ": " << i.mType.printPretty() << ",\n"; } decIndent(); os << indent() << "}";))
        if (i.discriminantValue) {
            os << " = " << i.discriminantValue;
        }
        os << ",\n";
        idx++;
    }
    decIndent();
    os << indent() << "}\n";
    os << "\n";
}

void RustPrinter::handleTrait(const ASTTrait& s) {
    printParams(s.params());
    {
        char c = ':';
        for (const auto& lft : s.lifetimes()) {
            os << " " << c << " " << lft.ent;
            c = '+';
        }
        for (const auto& t : s.supertraits()) {
            os << " " << c << " " << t.ent.hrbs << *t.ent.path;
            c = '+';
        }
    }
    os << "\n";
    printBounds(s.params());

    os << indent() << "{\n";
    incIndent();

    for (const auto& i : s.items()) {
        TU_MATCH_DEF(ASTItem, (i.data), (e), (), (Type, os << indent() << "type " << i.name << ";\n";), (Function, handleFunction(ASTVisibility::makeBarePrivate(), i.name, e);))
    }

    decIndent();
    os << indent() << "}\n";
    os << "\n";
}

void RustPrinter::handleFunction(const ASTVisibility& vis, const RcString& name, const ASTFunction& f) {
    os << indent();
    os << vis;
    if (f.isConst()) {
        os << "const ";
    }
    if (f.isUnsafe()) {
        os << "unsafe ";
    }
    if (f.isAsync()) {
        os << "async ";
    }
    if (f.abi() != ABI_RUST) {
        os << "extern \"" << f.abi() << "\" ";
    }
    os << "fn " << name;
    printParams(f.params());
    os << "(";
    bool isFirst = true;
    for (const auto& a : f.args()) {
        if (!isFirst) {
            os << ", ";
        }
        printAttrs(a.attrs);
        printPattern(a.pat, false);
        os << ": " << a.ty.printPretty();
        isFirst = false;
    }
    os << ")";
    if (!f.rettype().isUnit()) {
        os << " -> " << f.rettype().printPretty();
    }

    if (f.code().isValid()) {
        os << "\n";
        printBounds(f.params());

        os << indent();
        f.code().visitNodes(*this);
        os << "\n";
        //m_os << indent() << f.data.code() << "\n";
    } else {
        printBounds(f.params());
        os << ";\n";
    }
}

void RustPrinter::incIndent() {
    indentLevel++;
}

RepeatLitStr RustPrinter::indent() {
    return RepeatLitStr{"    ", indentLevel};
}

void RustPrinter::decIndent() {
    indentLevel--;
}

void DumpRust(const char* filename, const ASTCrate& crate) {
    ::std::ofstream os(filename);
    RustPrinter printer(os);
    printer.handleModule(crate.rootModule());
}

void DumpASTNode(::std::ostream& os, const ASTExprNode& node) {
    RustPrinter printer(os);
    const_cast<ASTExprNode&>(node).visit(printer);
}

#undef IS
#undef WRAPIF_CMD
#undef WRAPIF
