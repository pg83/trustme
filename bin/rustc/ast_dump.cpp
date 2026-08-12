#include "ast_dump.h"

#include "ast_crate.h"
#include "ast_ast.h"
#include "ast_expr.h"
#include "hir_hir.h" // ABI_RUST - TODO: Move elsewhere?
#include <fstream>
#include <limits> // std::numeric_limits
#include <string_view>

#include "cpp_unpack.h"

#define IS(v, c) (cast<c>(&v) != 0)
#define WRAPIF_CMD(v, t) || IS(v, t)
#define WRAPIF(uniq_ptr, class1, ...)                                   \
    do {                                                                \
        auto& _v = *(uniq_ptr);                                         \
        if (IS(_v, class1) CC_ITERATE(WRAPIF_CMD, (_v), __VA_ARGS__)) { \
            paren_wrap(uniq_ptr);                                       \
        } else {                                                        \
            AST::NodeVisitor::visit(uniq_ptr);                          \
        }                                                               \
    } while (0)

class RustPrinter: public AST::NodeVisitor {
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

    void handleModule(const AST::Module& mod);
    void handleStruct(const AST::Struct& s);
    void handleEnum(const AST::Enum& s);
    void handleTrait(const AST::Trait& s);

    void handleFunction(const AST::Visibility& vis, const RcString& name, const AST::Function& f);

    virtual bool is_const() const override {
        return true;
    }

    virtual void visit(AST::ExprNodeBlock& n) override {
        switch (n.blockType) {
            case AST::ExprNodeBlock::Type::Bare:
                break;
            case AST::ExprNodeBlock::Type::Unsafe:
                os << "unsafe ";
                break;
            case AST::ExprNodeBlock::Type::Const:
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
                this->print_attrs(child.node->attrs());
            }
            os << indent();
            exprRoot = true;
            if (!child.node.get()) {
                os << "/* nil */";
            } else {
                AST::NodeVisitor::visit(child.node);
            }
            if (child.hasSemicolon) {
                os << ";";
            }
        }
        os << "\n";
        decIndent();
        os << indent() << "}";
    }

    virtual void visit(AST::ExprNodeAsyncBlock& n) override {
        os << "async ";
        if (n.isMove) {
            os << "move ";
        }
        AST::NodeVisitor::visit(n.inner);
    }

    virtual void visit(AST::ExprNodeGeneratorBlock& n) override {
        os << "gen ";
        if (n.isMove) {
            os << "move ";
        }
        AST::NodeVisitor::visit(n.inner);
    }

    virtual void visit(AST::ExprNodeTry& n) override {
        os << "try ";
        AST::NodeVisitor::visit(n.inner);
    }

    void dumpToken(const Token& t) {
        os << t.to_str() << " ";
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

    virtual void visit(AST::ExprNodeMacro& n) override {
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

    virtual void visit(AST::ExprNodeAsm& n) override {
        os << "asm!( \"" << n.text << "\"";
        os << " :";
        for (auto& v : n.output) {
            os << " \"" << v.name << "\" (";
            AST::NodeVisitor::visit(v.value);
            os << "),";
        }
        os << " :";
        for (auto& v : n.input) {
            os << " \"" << v.name << "\" (";
            AST::NodeVisitor::visit(v.value);
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

    virtual void visit(AST::ExprNodeAsm2& n) override {
        os << "asm!( ";
        for (const auto& l : n.lines) {
            l.fmt(os);
            os << ", ";
        }
        for (auto& p : n.mParams) {
            TU_MATCH_HDRA((p), {)
            TU_ARMA(Const, e) {
                    os << "const ";
                    AST::NodeVisitor::visit(e);
                }
                TU_ARMA(Sym, e) {
                    os << "sym " << e;
                }
                TU_ARMA(RegSingle, e) {
                    os << e.dir << "(" << e.spec << ") ";
                    AST::NodeVisitor::visit(e.val);
                }
                TU_ARMA(Reg, e) {
                    os << e.dir << "(" << e.spec << ") ";
                    if (e.val_in) {
                        AST::NodeVisitor::visit(e.val_in);
                        if (e.val_out) {
                            os << " => ";
                        }
                    }
                    if (e.val_out) {
                        AST::NodeVisitor::visit(e.val_out);
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

    virtual void visit(AST::ExprNodeFlow& n) override {
        exprRoot = false;
        switch (n.mType) {
            case AST::ExprNodeFlow::RETURN:
                os << "return ";
                break;
            case AST::ExprNodeFlow::YIELD:
                os << "yield ";
                break;
            case AST::ExprNodeFlow::BREAK:
                os << "break ";
                break;
            case AST::ExprNodeFlow::CONTINUE:
                os << "continue ";
                break;
            case AST::ExprNodeFlow::YEET:
                os << "do yeet ";
                break;
        }
        if (n.target.name != "") {
            os << "'" << n.target << " ";
        }
        AST::NodeVisitor::visit(n.mValue);
    }

    virtual void visit(AST::ExprNodeLetBinding& n) override {
        exprRoot = false;
        os << "let ";
        print_pattern(n.pat, false);
        os << ": ";
        print_type(n.mType);
        if (n.mValue) {
            os << " = ";
            AST::NodeVisitor::visit(n.mValue);
        }
        if (n.elseNode) {
            os << " else ";
            AST::NodeVisitor::visit(n.elseNode);
        }
        os << ";";
    }

    virtual void visit(AST::ExprNodeAssign& n) override {
        exprRoot = false;
        AST::NodeVisitor::visit(n.slot);
        switch (n.op) {
            case AST::ExprNodeAssign::NONE:
                os << "  = ";
                break;
            case AST::ExprNodeAssign::ADD:
                os << " += ";
                break;
            case AST::ExprNodeAssign::SUB:
                os << " -= ";
                break;
            case AST::ExprNodeAssign::MUL:
                os << " *= ";
                break;
            case AST::ExprNodeAssign::DIV:
                os << " /= ";
                break;
            case AST::ExprNodeAssign::MOD:
                os << " %= ";
                break;
            case AST::ExprNodeAssign::AND:
                os << " &= ";
                break;
            case AST::ExprNodeAssign::OR:
                os << " |= ";
                break;
            case AST::ExprNodeAssign::XOR:
                os << " ^= ";
                break;
            case AST::ExprNodeAssign::SHR:
                os << " >>= ";
                break;
            case AST::ExprNodeAssign::SHL:
                os << " <<= ";
                break;
        }
        AST::NodeVisitor::visit(n.mValue);
    }

    virtual void visit(AST::ExprNodeCallPath& n) override {
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
            AST::NodeVisitor::visit(arg);
        }
        os << ")";
    }

    virtual void visit(AST::ExprNodeCallMethod& n) override {
        exprRoot = false;
        WRAPIF(n.val, AST::ExprNodeDeref, AST::ExprNodeUniOp, AST::ExprNodeCast, AST::ExprNodeBinOp, AST::ExprNodeAssign, AST::ExprNodeMatch, AST::ExprNodeIf, AST::ExprNodeMatch);
        os << "." << n.method;
        os << "(";
        bool isFirst = true;
        for (auto& arg : n.mArgs) {
            if (isFirst) {
                isFirst = false;
            } else {
                os << ", ";
            }
            AST::NodeVisitor::visit(arg);
        }
        os << ")";
    }

    virtual void visit(AST::ExprNodeCallObject& n) override {
        exprRoot = false;
        os << "(";
        AST::NodeVisitor::visit(n.val);
        os << ")(";
        bool isFirst = true;
        for (auto& arg : n.mArgs) {
            if (isFirst) {
                isFirst = false;
            } else {
                os << ", ";
            }
            AST::NodeVisitor::visit(arg);
        }
        os << ")";
    }

    virtual void visit(AST::ExprNodeLoop& n) override {
        bool expr_root = exprRoot;
        exprRoot = false;

        if (n.label.name != "") {
            os << "'" << n.label << ": ";
        }

        os << "loop";

        if (expr_root) {
            os << "\n";
            os << indent();
        } else {
            os << " ";
        }

        AST::NodeVisitor::visit(n.mCode);
    }

    virtual void visit(AST::ExprNodeFor& n) override {
        bool expr_root = exprRoot;
        exprRoot = false;

        if (n.label.name != "") {
            os << "'" << n.label << ": ";
        }
        os << "for ";
        print_pattern(n.pattern, true);
        os << " in ";
        AST::NodeVisitor::visit(n.mValue);

        if (expr_root) {
            os << "\n";
            os << indent();
        } else {
            os << " ";
        }

        AST::NodeVisitor::visit(n.mCode);
    }

    void visit_iflet_conditions(std::vector<AST::IfLetCondition>& conds) {
        for (size_t i = 0; i < conds.size(); i++) {
            if (i != 0) {
                os << " && ";
            }
            if (conds[i].optPat) {
                os << "let ";
                print_pattern(*conds[i].optPat, true);
                os << " = ";
            }
            os << "(";
            AST::NodeVisitor::visit(conds[i].value);
            os << ")";
        }
    }

    void visit(AST::ExprNodeWhile& n) override {
        bool expr_root = exprRoot;
        exprRoot = false;

        if (n.label.name != "") {
            os << "'" << n.label << ": ";
        }

        os << "while ";
        visit_iflet_conditions(n.conditions);
        if (expr_root) {
            os << "\n";
            os << indent();
        } else {
            os << " ";
        }

        AST::NodeVisitor::visit(n.mCode);
    }

    virtual void visit(AST::ExprNodeMatch& n) override {
        bool expr_root = exprRoot;
        exprRoot = false;
        os << "match ";
        AST::NodeVisitor::visit(n.val);

        if (expr_root) {
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
                print_pattern(pat, true);
            }
            if (!arm.guard.empty()) {
                os << " if ";
                visit_iflet_conditions(arm.guard);
            }
            os << " => ";
            // Increase indent, but don't print. Causes nested blocks to be indented above the match
            incIndent();
            AST::NodeVisitor::visit(arm.mCode);
            decIndent();
            os << ",\n";
        }

        if (expr_root) {
            os << indent() << "}";
        } else {
            os << indent() << "}";
            decIndent();
        }
    }

    virtual void visit(AST::ExprNodeIf& n) override {
        bool expr_root = exprRoot;
        exprRoot = false;
        for (auto& arm : n.arms) {
            if (&arm != n.arms.data()) {
                if (expr_root) {
                    os << indent();
                }
                os << "else ";
            }

            os << "if ";
            visit_iflet_conditions(arm.conditions);

            bool isBlock = (cast<const AST::ExprNodeBlock>(&*arm.body) != nullptr);
            if (!isBlock) {
                os << "{ ";
            }
            AST::NodeVisitor::visit(arm.body);
            if (!isBlock) {
                os << " }";
            }
            if (expr_root) {
                os << "\n";
            }
        }
        if (n.elseNode) {
            if (expr_root) {
                os << indent();
            }
            os << "else";
            bool isBlock = (cast<const AST::ExprNodeBlock>(&*n.elseNode) != nullptr);
            if (!isBlock) {
                os << "{ ";
            }
            AST::NodeVisitor::visit(n.elseNode);
            if (!isBlock) {
                os << " }";
            }
        }
    }

    virtual void visit(AST::ExprNodeClosure& n) override {
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
            print_pattern(arg.first, false);
            os << ": ";
            print_type(arg.second);
        }
        os << "| ->";
        print_type(n.returnType);
        os << " { ";
        AST::NodeVisitor::visit(n.mCode);
        os << " }";
    }

    virtual void visit(AST::ExprNodeWildcardPattern& n) override {
        os << "_";
    }

    virtual void visit(AST::ExprNodeInteger& n) override {
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
                    switch (n.mValue.truncate_u64()) {
                        case '\'':
                            os << "'\\''";
                            break;
                        case '\\':
                            os << "'\\\\'";
                            break;
                        default:
                            os << "'" << (char)n.mValue.truncate_u64() << "'";
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

    virtual void visit(AST::ExprNodeFloat& n) override {
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

    virtual void visit(AST::ExprNodeBool& n) override {
        exprRoot = false;
        if (n.mValue) {
            os << "true";
        } else {
            os << "false";
        }
    }

    virtual void visit(AST::ExprNodeString& n) override {
        exprRoot = false;
        os << "\"" << FmtEscaped(n.mValue) << "\"";
    }

    virtual void visit(AST::ExprNodeByteString& n) override {
        exprRoot = false;
        os << "b\"" << FmtEscaped(n.mValue) << "\"";
    }

    virtual void visit(AST::ExprNodeCString& n) override {
        exprRoot = false;
        os << "c\"" << FmtEscaped(n.mValue) << "\"";
    }

    virtual void visit(AST::ExprNodeStructLiteral& n) override {
        exprRoot = false;
        os << n.mPath << " {\n";
        incIndent();
        for (auto& i : n.values) {
            print_attrs(i.attrs);
            os << indent() << "r#" << i.name << ": ";
            AST::NodeVisitor::visit(i.value);
            os << ",\n";
        }
        if (n.baseValue.get()) {
            os << indent() << ".. ";
            AST::NodeVisitor::visit(n.baseValue);
            os << "\n";
        }
        os << indent() << "}";
        decIndent();
    }

    virtual void visit(AST::ExprNodeStructLiteralPattern& n) override {
        exprRoot = false;
        os << n.mPath << " {\n";
        incIndent();
        for (auto& i : n.values) {
            print_attrs(i.attrs);
            os << indent() << "r#" << i.name << ": ";
            AST::NodeVisitor::visit(i.value);
            os << ",\n";
        }
        os << indent() << "..\n";
        os << indent() << "}";
        decIndent();
    }

    virtual void visit(AST::ExprNodeArray& n) override {
        exprRoot = false;
        os << "[";
        if (n.mSize.get()) {
            AST::NodeVisitor::visit(n.values[0]);
            os << "; ";
            AST::NodeVisitor::visit(n.mSize);
        } else {
            for (auto& item : n.values) {
                AST::NodeVisitor::visit(item);
                os << ", ";
            }
        }
        os << "]";
    }

    virtual void visit(AST::ExprNodeTuple& n) override {
        exprRoot = false;
        os << "(";
        for (auto& item : n.values) {
            AST::NodeVisitor::visit(item);
            os << ", ";
        }
        os << ")";
    }

    virtual void visit(AST::ExprNodeNamedValue& n) override {
        exprRoot = false;
        os << n.mPath;
    }

    virtual void visit(AST::ExprNodeField& n) override {
        exprRoot = false;
        WRAPIF(n.obj, AST::ExprNodeDeref, AST::ExprNodeUniOp, AST::ExprNodeCast, AST::ExprNodeBinOp, AST::ExprNodeAssign, AST::ExprNodeMatch, AST::ExprNodeIf, AST::ExprNodeMatch);
        os << "." << n.mName;
    }

    virtual void visit(AST::ExprNodeIndex& n) override {
        exprRoot = false;
        WRAPIF(n.obj, AST::ExprNodeDeref, AST::ExprNodeUniOp, AST::ExprNodeCast, AST::ExprNodeBinOp, AST::ExprNodeAssign, AST::ExprNodeMatch, AST::ExprNodeIf, AST::ExprNodeMatch);
        os << "[";
        AST::NodeVisitor::visit(n.idx);
        os << "]";
    }

    virtual void visit(AST::ExprNodeDeref& n) override {
        exprRoot = false;
        os << "*(";
        AST::NodeVisitor::visit(n.mValue);
        os << ")";
    }

    virtual void visit(AST::ExprNodeCast& n) override {
        exprRoot = false;
        os << "(";
        AST::NodeVisitor::visit(n.mValue);
        os << ") as " << n.mType;
    }

    virtual void visit(AST::ExprNodeTypeAnnotation& n) override {
        exprRoot = false;
        os << "(";
        AST::NodeVisitor::visit(n.mValue);
        os << ") : " << n.mType;
    }

    virtual void visit(AST::ExprNodeBinOp& n) override {
        exprRoot = false;
        auto* leftBinop = cast<AST::ExprNodeBinOp>(n.left.get());
        if (!n.left) {
            os << "/*null*/";
        } else if (leftBinop && leftBinop->mType == n.mType) {
            AST::NodeVisitor::visit(n.left);
        } else {
            WRAPIF(n.left, AST::ExprNodeCast, AST::ExprNodeBinOp);
        }
        os << " ";
        switch (n.mType) {
            case AST::ExprNodeBinOp::CMPEQU:
                os << "==";
                break;
            case AST::ExprNodeBinOp::CMPNEQU:
                os << "!=";
                break;
            case AST::ExprNodeBinOp::CMPLT:
                os << "<";
                break;
            case AST::ExprNodeBinOp::CMPLTE:
                os << "<=";
                break;
            case AST::ExprNodeBinOp::CMPGT:
                os << ">";
                break;
            case AST::ExprNodeBinOp::CMPGTE:
                os << ">=";
                break;
            case AST::ExprNodeBinOp::BOOLAND:
                os << "&&";
                break;
            case AST::ExprNodeBinOp::BOOLOR:
                os << "||";
                break;
            case AST::ExprNodeBinOp::BITAND:
                os << "&";
                break;
            case AST::ExprNodeBinOp::BITOR:
                os << "|";
                break;
            case AST::ExprNodeBinOp::BITXOR:
                os << "^";
                break;
            case AST::ExprNodeBinOp::SHL:
                os << "<<";
                break;
            case AST::ExprNodeBinOp::SHR:
                os << ">>";
                break;
            case AST::ExprNodeBinOp::MULTIPLY:
                os << "*";
                break;
            case AST::ExprNodeBinOp::DIVIDE:
                os << "/";
                break;
            case AST::ExprNodeBinOp::MODULO:
                os << "%";
                break;
            case AST::ExprNodeBinOp::ADD:
                os << "+";
                break;
            case AST::ExprNodeBinOp::SUB:
                os << "-";
                break;
            case AST::ExprNodeBinOp::RANGE:
                os << "..";
                break;
            case AST::ExprNodeBinOp::RANGE_INC:
                os << "...";
                break;
            case AST::ExprNodeBinOp::PLACE_IN:
                os << "<-";
                break;
        }
        os << " ";
        auto* right_binop = cast<AST::ExprNodeBinOp>(n.right.get());
        if (!n.right) {
            os << "/*null*/";
        } else if (right_binop && right_binop->mType != n.mType) {
            paren_wrap(n.right);
        } else {
            AST::NodeVisitor::visit(n.right);
        }
    }

    virtual void visit(AST::ExprNodeUniOp& n) override {
        exprRoot = false;
        switch (n.mType) {
            case AST::ExprNodeUniOp::NEGATE:
                os << "-";
                break;
            case AST::ExprNodeUniOp::INVERT:
                os << "!";
                break;
            case AST::ExprNodeUniOp::BOX:
                os << "box ";
                break;
            case AST::ExprNodeUniOp::REF:
                os << "&";
                break;
            case AST::ExprNodeUniOp::REFMUT:
                os << "&mut ";
                break;
            case AST::ExprNodeUniOp::RawBorrow:
                os << "&raw const ";
                break;
            case AST::ExprNodeUniOp::RawBorrowMut:
                os << "&raw mut ";
                break;
            case AST::ExprNodeUniOp::QMARK:
                break;
            case AST::ExprNodeUniOp::AWait:
                break;
        }

        bool wrap = IS(*n.mValue, AST::ExprNodeBinOp) || IS(*n.mValue, AST::ExprNodeCast);
        if (wrap) {
            os << "(";
        }
        AST::NodeVisitor::visit(n.mValue);
        if (wrap) {
            os << ")";
        }
        switch (n.mType) {
            case AST::ExprNodeUniOp::QMARK:
                os << "?";
                break;
            case AST::ExprNodeUniOp::AWait:
                os << ".await";
                break;
            default:
                break;
        }
    }

    virtual void visit(AST::ExprNodeMacroDefinition& n) override {
        os << "/* macro definition #" << n.definitionId << " */";
    }

private:
    void paren_wrap(::AST::ExprNodeP& node) {
        os << "(";
        AST::NodeVisitor::visit(node);
        os << ")";
    }

    void print_attrs(const AST::AttributeList& attrs);
    void print_params(const AST::GenericParams& params);
    void print_bounds(const AST::GenericParams& params);
    void print_pattern_tuple(const AST::Pattern::TuplePat& v, bool isRefutable);
    void print_pattern(const AST::Pattern& p, bool isRefutable);
    void print_type(const TypeRef& t);

    void incIndent();
    RepeatLitStr indent();
    void decIndent();
};

void RustPrinter::print_attrs(const AST::AttributeList& attrs) {
    for (const auto& a : attrs.mItems) {
        os << indent() << "#[" << a << "]\n";
    }
}

void RustPrinter::handleModule(const AST::Module& mod) {
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

        print_attrs(item.attrs);
        os << indent() << "extern crate \"" << e.name << "\" as " << item.name << ";\n";
    }

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_ExternBlock()) {
            continue;
        }
        const auto& e = item.data.as_ExternBlock();

        print_attrs(item.attrs);
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
        print_attrs(item.attrs);
        os << indent() << item.vis << "type " << item.name;
        print_params(e.params());
        os << " = " << e.type();
        print_bounds(e.params());
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
        print_attrs(item.attrs);
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
        print_attrs(item.attrs);
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
        print_attrs(item.attrs);
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
        print_attrs(item.attrs);
        os << indent() << item.vis;
        switch (e.s_class()) {
            case AST::Static::CONST:
                os << "const ";
                break;
            case AST::Static::STATIC:
                os << "static ";
                break;
            case AST::Static::MUT:
                os << "static mut ";
                break;
        }
        os << item.name << ": " << e.type() << " = ";
        e.value().visit_nodes(*this);
        os << ";\n";
    }

    for (const auto& ip : mod.mItems) {
        const auto& item = *ip;
        if (!item.data.is_Function()) {
            continue;
        }
        const auto& e = item.data.as_Function();

        os << "\n";
        print_attrs(item.attrs);
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
        if (i.def().is_const()) {
            os << " const";
        }
        print_params(i.def().params());
        if (i.def().trait().ent != AST::Path()) {
            os << " " << i.def().trait().ent << " for";
        }
        os << " " << i.def().type() << "\n";

        print_bounds(i.def().params());
        os << indent() << "{\n";
        incIndent();
        for (const auto& it : i.items()) {
            TU_MATCH_DEF(
                AST::Item,
                (*it.data),
                (e),
                (throw ::std::runtime_error(FMT("Unexpected item type in impl block - " << it.data->tag_str()));),
                (
                    None,
                    // Ignore, it's been deleted by #[cfg]
                ),
                (
                    MacroInv,
                    // TODO: Dump macro invocations
                ),
                (
                    Static, os << indent(); switch (e.s_class()) {
                        case ::AST::Static::CONST:
                            os << "const ";
                            break;
                        case ::AST::Static::STATIC:
                            os << "static ";
                            break;
                        case ::AST::Static::MUT:
                            os << "static mut ";
                            break;
                    } os << it.name
                           << ": " << e.type() << " = ";
                    e.value().visit_nodes(*this);
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

void RustPrinter::print_params(const AST::GenericParams& params) {
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

void RustPrinter::print_bounds(const AST::GenericParams& params) {
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
            TU_MATCH(AST::GenericBound, (b), (ent), (None, os << "/*-*/";), (Lifetime, os << ent.test << ": " << ent.bound;), (TypeLifetime, os << ent.type << ": " << ent.bound;), (IsTrait, os << ent.outer_hrbs << ent.type << ": "; if (ent.constness == AST::BoundConstness::Always) os << "const "; else if (ent.constness == AST::BoundConstness::Maybe) os << "[const] "; os << ent.innerHrbs << ent.trait;), (MaybeTrait, os << ent.type << ": ?" << ent.trait;), (NotTrait, os << ent.type << ": !" << ent.trait;), (Equality, os << ent.type << ": =" << ent.replacement;))
        }
        os << "\n";

        decIndent();
    }
}

void RustPrinter::print_pattern_tuple(const AST::Pattern::TuplePat& v, bool isRefutable) {
    for (const auto& sp : v.start) {
        print_pattern(sp, isRefutable);
        os << ", ";
    }
    if (v.hasWildcard) {
        os << ".., ";
        for (const auto& sp : v.end) {
            print_pattern(sp, isRefutable);
            os << ", ";
        }
    }
}

void RustPrinter::print_pattern(const AST::Pattern& p, bool isRefutable) {
    for (const auto& pb : p.bindings()) {
        if (pb.isMutable) {
            os << "mut ";
        }
        switch (pb.mType) {
            case ::AST::PatternBinding::Type::MOVE:
                break;
            case ::AST::PatternBinding::Type::REF:
                os << "ref ";
                break;
            case ::AST::PatternBinding::Type::MUTREF:
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
        AST::Pattern::Data,
        (p.data()),
        (v),
        (Any, os << "_";),
        (MaybeBind, os << v.name << " /*?*/";),
        (Macro, os << *v.inv;),
        (Box,
         {
             const auto& v = p.data().as_Box();
             os << "box ";
             print_pattern(*v.sub, isRefutable);
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
             print_pattern(*v.sub, isRefutable);
             os << ")";
         }),
        (Value, os << v.start; if (!v.end.is_Invalid()) { os << " ..= " << v.end; }),
        (ValueLeftInc, os << v.start << " .. " << v.end;),
        (StructTuple, os << v.path << "("; this->print_pattern_tuple(v.tup_pat, isRefutable); os << ")";),
        (Struct,
         {
             const auto& v = p.data().as_Struct();
             os << v.path << "{";
             for (const auto& sp : v.sub_patterns) {
                 os << sp.name << ": ";
                 print_pattern(sp.pat, isRefutable);
                 os << ",";
             }
             if (!v.isExhaustive) {
                 os << "..";
             }
             os << "}";
         }),
        (Tuple, os << "("; this->print_pattern_tuple(v, isRefutable); os << ")";),
        (
            Slice, os << "["; for (const auto& sp : v.sub_pats) {
                print_pattern(sp, isRefutable);
                os << ", ";
            } os << "]";
        ),
        (
            SplitSlice, os << "["; bool needsComma = false; for (const auto& sp : v.leading) {
                print_pattern(sp, isRefutable);
                os << ", ";
            }

                                                               if (v.extraBind.isValid()) {
                                                                   const auto& b = v.extraBind;
                                                                   if (b.isMutable) {
                                                                       os << "mut ";
                                                                   }
                                                                   switch (b.mType) {
                                                                       case ::AST::PatternBinding::Type::MOVE:
                                                                           break;
                                                                       case ::AST::PatternBinding::Type::REF:
                                                                           os << "ref ";
                                                                           break;
                                                                       case ::AST::PatternBinding::Type::MUTREF:
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
                    print_pattern(sp, isRefutable);
                    os << ", ";
                }
            } os
            << "]";
        ),
        (Or, os << "("; for (const auto& e : v) {
            os << (&e == &v.front() ? "" : " | ");
            print_pattern(e, isRefutable);
        } os << ")";)
    )
}

void RustPrinter::print_type(const TypeRef& t) {
    os << t;
}

void RustPrinter::handleStruct(const AST::Struct& s) {
    print_params(s.params());

    TU_MATCH(
        AST::StructData,
        (s.mData),
        (e),
        (Unit, os << " /* unit-like */\n"; print_bounds(s.params()); os << indent() << ";\n";),
        (Tuple, os << "("; for (const auto& i : e.ents) { os << i.vis << i.mType << ", "; } os << ")\n"; print_bounds(s.params()); os << indent() << ";\n";),
        (Struct, os << "\n"; print_bounds(s.params());

         os << indent() << "{\n";
         incIndent();
         for (const auto& i : e.ents) { os << indent() << i.vis << i.mName << ": " << i.mType.print_pretty() << ",\n"; } decIndent();
         os << indent() << "}\n";)
    )
    os << "\n";
}

void RustPrinter::handleEnum(const AST::Enum& s) {
    print_params(s.params());
    os << "\n";
    print_bounds(s.params());

    os << indent() << "{\n";
    incIndent();
    unsigned int idx = 0;
    for (const auto& i : s.variants()) {
        os << indent() << "/*" << idx << "*/" << i.mName;
        TU_MATCH(AST::EnumVariantData, (i.mData), (e), (Unit, ), (Tuple, os << "("; for (const auto& t : e.mItems) os << t.mType.print_pretty() << ", "; os << ")";), (Struct, os << "{\n"; incIndent(); for (const auto& i : e.fields) { os << indent() << i.mName << ": " << i.mType.print_pretty() << ",\n"; } decIndent(); os << indent() << "}";))
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

void RustPrinter::handleTrait(const AST::Trait& s) {
    print_params(s.params());
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
    print_bounds(s.params());

    os << indent() << "{\n";
    incIndent();

    for (const auto& i : s.items()) {
        TU_MATCH_DEF(AST::Item, (i.data), (e), (), (Type, os << indent() << "type " << i.name << ";\n";), (Function, handleFunction(AST::Visibility::makeBarePrivate(), i.name, e);))
    }

    decIndent();
    os << indent() << "}\n";
    os << "\n";
}

void RustPrinter::handleFunction(const AST::Visibility& vis, const RcString& name, const AST::Function& f) {
    os << indent();
    os << vis;
    if (f.is_const()) {
        os << "const ";
    }
    if (f.is_unsafe()) {
        os << "unsafe ";
    }
    if (f.isAsync()) {
        os << "async ";
    }
    if (f.abi() != ABI_RUST) {
        os << "extern \"" << f.abi() << "\" ";
    }
    os << "fn " << name;
    print_params(f.params());
    os << "(";
    bool isFirst = true;
    for (const auto& a : f.args()) {
        if (!isFirst) {
            os << ", ";
        }
        print_attrs(a.attrs);
        print_pattern(a.pat, false);
        os << ": " << a.ty.print_pretty();
        isFirst = false;
    }
    os << ")";
    if (!f.rettype().isUnit()) {
        os << " -> " << f.rettype().print_pretty();
    }

    if (f.code().isValid()) {
        os << "\n";
        print_bounds(f.params());

        os << indent();
        f.code().visit_nodes(*this);
        os << "\n";
        //m_os << indent() << f.data.code() << "\n";
    } else {
        print_bounds(f.params());
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

void DumpRust(const char* filename, const AST::Crate& crate) {
    ::std::ofstream os(filename);
    RustPrinter printer(os);
    printer.handleModule(crate.root_module());
}

void DumpASTNode(::std::ostream& os, const AST::ExprNode& node) {
    RustPrinter printer(os);
    const_cast<AST::ExprNode&>(node).visit(printer);
}

#undef IS
#undef WRAPIF_CMD
#undef WRAPIF
