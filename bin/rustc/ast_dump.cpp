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
    ::std::ostream& m_os;
    int m_indent_level;
    bool m_expr_root; //!< used to allow 'if' and 'match' to behave differently as standalone exprs
public:
    RustPrinter(::std::ostream& os)
        : m_os(os)
        , m_indent_level(0)
        , m_expr_root(false)
    {
    }

    void handle_module(const AST::Module& mod);
    void handle_struct(const AST::Struct& s);
    void handle_enum(const AST::Enum& s);
    void handle_trait(const AST::Trait& s);

    void handle_function(const AST::Visibility& vis, const RcString& name, const AST::Function& f);

    virtual bool is_const() const override {
        return true;
    }

    virtual void visit(AST::ExprNodeBlock& n) override {
        switch (n.m_block_type) {
            case AST::ExprNodeBlock::Type::Bare:
                break;
            case AST::ExprNodeBlock::Type::Unsafe:
                m_os << "unsafe ";
                break;
            case AST::ExprNodeBlock::Type::Const:
                m_os << "const ";
                break;
        }
        if (n.m_label.name != RcString()) {
            m_os << "'" << n.m_label << ": ";
        }
        m_os << "{";
        inc_indent();
        if (n.m_local_mod) {
            m_os << "\n";
            m_os << indent() << "// ANON: " << n.m_local_mod->path() << "\n";
            handle_module(*n.m_local_mod);
        }
        for (auto& child : n.m_nodes) {
            m_os << "\n";
            if (child.node) {
                this->print_attrs(child.node->attrs());
            }
            m_os << indent();
            m_expr_root = true;
            if (!child.node.get()) {
                m_os << "/* nil */";
            } else {
                AST::NodeVisitor::visit(child.node);
            }
            if (child.has_semicolon) {
                m_os << ";";
            }
        }
        m_os << "\n";
        dec_indent();
        m_os << indent() << "}";
    }

    virtual void visit(AST::ExprNodeAsyncBlock& n) override {
        m_os << "async ";
        if (n.m_is_move) {
            m_os << "move ";
        }
        AST::NodeVisitor::visit(n.m_inner);
    }

    virtual void visit(AST::ExprNodeGeneratorBlock& n) override {
        m_os << "gen ";
        if (n.m_is_move) {
            m_os << "move ";
        }
        AST::NodeVisitor::visit(n.m_inner);
    }

    virtual void visit(AST::ExprNodeTry& n) override {
        m_os << "try ";
        AST::NodeVisitor::visit(n.m_inner);
    }

    void dump_token(const Token& t) {
        m_os << t.to_str() << " ";
    }

    void dump_tokentree(const TokenTree& tt) {
        if (tt.is_token()) {
            dump_token(tt.tok());
        } else {
            for (size_t i = 0; i < tt.size(); i++) {
                dump_tokentree(tt[i]);
            }
        }
    }

    virtual void visit(AST::ExprNodeMacro& n) override {
        m_expr_root = false;
        m_os << n.m_path << "!";
        if (n.m_ident != "") {
            m_os << " ";
            m_os << n.m_ident;
        }
        m_os << (n.m_is_braced ? "{" : "(");
        dump_tokentree(n.m_tokens);
        m_os << (n.m_is_braced ? "}" : ")");
    }

    virtual void visit(AST::ExprNodeAsm& n) override {
        m_os << "asm!( \"" << n.m_text << "\"";
        m_os << " :";
        for (auto& v : n.m_output) {
            m_os << " \"" << v.name << "\" (";
            AST::NodeVisitor::visit(v.value);
            m_os << "),";
        }
        m_os << " :";
        for (auto& v : n.m_input) {
            m_os << " \"" << v.name << "\" (";
            AST::NodeVisitor::visit(v.value);
            m_os << "),";
        }
        m_os << " :";
        for (const auto& v : n.m_clobbers) {
            m_os << " \"" << v << "\",";
        }
        m_os << " :";
        for (const auto& v : n.m_flags) {
            m_os << " \"" << v << "\",";
        }
        m_os << " )";
    }

    virtual void visit(AST::ExprNodeAsm2& n) override {
        m_os << "asm!( ";
        for (const auto& l : n.m_lines) {
            l.fmt(m_os);
            m_os << ", ";
        }
        for (auto& p : n.m_params) {
            TU_MATCH_HDRA((p), {)
            TU_ARMA(Const, e) {
                    m_os << "const ";
                    AST::NodeVisitor::visit(e);
                }
                TU_ARMA(Sym, e) {
                    m_os << "sym " << e;
                }
                TU_ARMA(RegSingle, e) {
                    m_os << e.dir << "(" << e.spec << ") ";
                    AST::NodeVisitor::visit(e.val);
                }
                TU_ARMA(Reg, e) {
                    m_os << e.dir << "(" << e.spec << ") ";
                    if (e.val_in) {
                        AST::NodeVisitor::visit(e.val_in);
                        if (e.val_out) {
                            m_os << " => ";
                        }
                    }
                    if (e.val_out) {
                        AST::NodeVisitor::visit(e.val_out);
                    }
                }
            }
            m_os << ", ";
        }
        if (n.m_options.any()) {
            n.m_options.fmt(m_os);
            //m_os << "options(";
            //m_os << ")";
        }
        m_os << ")";
    }

    virtual void visit(AST::ExprNodeFlow& n) override {
        m_expr_root = false;
        switch (n.m_type) {
            case AST::ExprNodeFlow::RETURN:
                m_os << "return ";
                break;
            case AST::ExprNodeFlow::YIELD:
                m_os << "yield ";
                break;
            case AST::ExprNodeFlow::BREAK:
                m_os << "break ";
                break;
            case AST::ExprNodeFlow::CONTINUE:
                m_os << "continue ";
                break;
            case AST::ExprNodeFlow::YEET:
                m_os << "do yeet ";
                break;
        }
        if (n.m_target.name != "") {
            m_os << "'" << n.m_target << " ";
        }
        AST::NodeVisitor::visit(n.m_value);
    }

    virtual void visit(AST::ExprNodeLetBinding& n) override {
        m_expr_root = false;
        m_os << "let ";
        print_pattern(n.m_pat, false);
        m_os << ": ";
        print_type(n.m_type);
        if (n.m_value) {
            m_os << " = ";
            AST::NodeVisitor::visit(n.m_value);
        }
        if (n.m_else) {
            m_os << " else ";
            AST::NodeVisitor::visit(n.m_else);
        }
        m_os << ";";
    }

    virtual void visit(AST::ExprNodeAssign& n) override {
        m_expr_root = false;
        AST::NodeVisitor::visit(n.m_slot);
        switch (n.m_op) {
            case AST::ExprNodeAssign::NONE:
                m_os << "  = ";
                break;
            case AST::ExprNodeAssign::ADD:
                m_os << " += ";
                break;
            case AST::ExprNodeAssign::SUB:
                m_os << " -= ";
                break;
            case AST::ExprNodeAssign::MUL:
                m_os << " *= ";
                break;
            case AST::ExprNodeAssign::DIV:
                m_os << " /= ";
                break;
            case AST::ExprNodeAssign::MOD:
                m_os << " %= ";
                break;
            case AST::ExprNodeAssign::AND:
                m_os << " &= ";
                break;
            case AST::ExprNodeAssign::OR:
                m_os << " |= ";
                break;
            case AST::ExprNodeAssign::XOR:
                m_os << " ^= ";
                break;
            case AST::ExprNodeAssign::SHR:
                m_os << " >>= ";
                break;
            case AST::ExprNodeAssign::SHL:
                m_os << " <<= ";
                break;
        }
        AST::NodeVisitor::visit(n.m_value);
    }

    virtual void visit(AST::ExprNodeCallPath& n) override {
        m_expr_root = false;
        m_os << n.m_path;
        m_os << "(";
        bool is_first = true;
        for (auto& arg : n.m_args) {
            if (is_first) {
                is_first = false;
            } else {
                m_os << ", ";
            }
            AST::NodeVisitor::visit(arg);
        }
        m_os << ")";
    }

    virtual void visit(AST::ExprNodeCallMethod& n) override {
        m_expr_root = false;
        WRAPIF(n.m_val, AST::ExprNodeDeref, AST::ExprNodeUniOp, AST::ExprNodeCast, AST::ExprNodeBinOp, AST::ExprNodeAssign, AST::ExprNodeMatch, AST::ExprNodeIf, AST::ExprNodeMatch);
        m_os << "." << n.m_method;
        m_os << "(";
        bool is_first = true;
        for (auto& arg : n.m_args) {
            if (is_first) {
                is_first = false;
            } else {
                m_os << ", ";
            }
            AST::NodeVisitor::visit(arg);
        }
        m_os << ")";
    }

    virtual void visit(AST::ExprNodeCallObject& n) override {
        m_expr_root = false;
        m_os << "(";
        AST::NodeVisitor::visit(n.m_val);
        m_os << ")(";
        bool is_first = true;
        for (auto& arg : n.m_args) {
            if (is_first) {
                is_first = false;
            } else {
                m_os << ", ";
            }
            AST::NodeVisitor::visit(arg);
        }
        m_os << ")";
    }

    virtual void visit(AST::ExprNodeLoop& n) override {
        bool expr_root = m_expr_root;
        m_expr_root = false;

        if (n.m_label.name != "") {
            m_os << "'" << n.m_label << ": ";
        }

        m_os << "loop";

        if (expr_root) {
            m_os << "\n";
            m_os << indent();
        } else {
            m_os << " ";
        }

        AST::NodeVisitor::visit(n.m_code);
    }

    virtual void visit(AST::ExprNodeFor& n) override {
        bool expr_root = m_expr_root;
        m_expr_root = false;

        if (n.m_label.name != "") {
            m_os << "'" << n.m_label << ": ";
        }
        m_os << "for ";
        print_pattern(n.m_pattern, true);
        m_os << " in ";
        AST::NodeVisitor::visit(n.m_value);

        if (expr_root) {
            m_os << "\n";
            m_os << indent();
        } else {
            m_os << " ";
        }

        AST::NodeVisitor::visit(n.m_code);
    }

    void visit_iflet_conditions(std::vector<AST::IfLet_Condition>& conds) {
        for (size_t i = 0; i < conds.size(); i++) {
            if (i != 0) {
                m_os << " && ";
            }
            if (conds[i].opt_pat) {
                m_os << "let ";
                print_pattern(*conds[i].opt_pat, true);
                m_os << " = ";
            }
            m_os << "(";
            AST::NodeVisitor::visit(conds[i].value);
            m_os << ")";
        }
    }

    void visit(AST::ExprNodeWhile& n) override {
        bool expr_root = m_expr_root;
        m_expr_root = false;

        if (n.m_label.name != "") {
            m_os << "'" << n.m_label << ": ";
        }

        m_os << "while ";
        visit_iflet_conditions(n.m_conditions);
        if (expr_root) {
            m_os << "\n";
            m_os << indent();
        } else {
            m_os << " ";
        }

        AST::NodeVisitor::visit(n.m_code);
    }

    virtual void visit(AST::ExprNodeMatch& n) override {
        bool expr_root = m_expr_root;
        m_expr_root = false;
        m_os << "match ";
        AST::NodeVisitor::visit(n.m_val);

        if (expr_root) {
            m_os << "\n";
            m_os << indent() << "{\n";
        } else {
            m_os << " {\n";
            inc_indent();
        }

        for (auto& arm : n.m_arms) {
            m_os << indent();
            bool is_first = true;
            for (const auto& pat : arm.m_patterns) {
                if (!is_first) {
                    m_os << "|";
                }
                is_first = false;
                print_pattern(pat, true);
            }
            if (!arm.m_guard.empty()) {
                m_os << " if ";
                visit_iflet_conditions(arm.m_guard);
            }
            m_os << " => ";
            // Increase indent, but don't print. Causes nested blocks to be indented above the match
            inc_indent();
            AST::NodeVisitor::visit(arm.m_code);
            dec_indent();
            m_os << ",\n";
        }

        if (expr_root) {
            m_os << indent() << "}";
        } else {
            m_os << indent() << "}";
            dec_indent();
        }
    }

    virtual void visit(AST::ExprNodeIf& n) override {
        bool expr_root = m_expr_root;
        m_expr_root = false;
        for (auto& arm : n.m_arms) {
            if (&arm != n.m_arms.data()) {
                if (expr_root) {
                    m_os << indent();
                }
                m_os << "else ";
            }

            m_os << "if ";
            visit_iflet_conditions(arm.m_conditions);

            bool is_block = (cast<const AST::ExprNodeBlock>(&*arm.m_body) != nullptr);
            if (!is_block) {
                m_os << "{ ";
            }
            AST::NodeVisitor::visit(arm.m_body);
            if (!is_block) {
                m_os << " }";
            }
            if (expr_root) {
                m_os << "\n";
            }
        }
        if (n.m_else) {
            if (expr_root) {
                m_os << indent();
            }
            m_os << "else";
            bool is_block = (cast<const AST::ExprNodeBlock>(&*n.m_else) != nullptr);
            if (!is_block) {
                m_os << "{ ";
            }
            AST::NodeVisitor::visit(n.m_else);
            if (!is_block) {
                m_os << " }";
            }
        }
    }

    virtual void visit(AST::ExprNodeClosure& n) override {
        m_expr_root = false;
        if (n.m_is_move) {
            m_os << "move ";
        }
        m_os << "|";
        bool is_first = true;
        for (const auto& arg : n.m_args) {
            if (!is_first) {
                m_os << ", ";
            }
            is_first = false;
            print_pattern(arg.first, false);
            m_os << ": ";
            print_type(arg.second);
        }
        m_os << "| ->";
        print_type(n.m_return);
        m_os << " { ";
        AST::NodeVisitor::visit(n.m_code);
        m_os << " }";
    }

    virtual void visit(AST::ExprNodeWildcardPattern& n) override {
        m_os << "_";
    }

    virtual void visit(AST::ExprNodeInteger& n) override {
        m_expr_root = false;
        switch (n.m_datatype) {
            case CORETYPE_INVAL:
                m_os << "0x" << ::std::hex << n.m_value << ::std::dec << "_/*INVAL*/";
                break;
            case CORETYPE_BOOL:
            case CORETYPE_STR:
                m_os << "0x" << ::std::hex << n.m_value << ::std::dec << "_/*bool/str*/";
                break;
            case CORETYPE_CHAR:
                //if( 0x20 <= n.m_value && n.m_value < 128 ) {
                if (n.m_value >= 0x20 && n.m_value < 128) {
                    switch (n.m_value.truncate_u64()) {
                        case '\'':
                            m_os << "'\\''";
                            break;
                        case '\\':
                            m_os << "'\\\\'";
                            break;
                        default:
                            m_os << "'" << (char)n.m_value.truncate_u64() << "'";
                            break;
                    }
                } else {
                    m_os << "'\\u{" << ::std::hex << n.m_value << ::std::dec << "}'";
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
                m_os << "0x" << ::std::hex << n.m_value << ::std::dec;
                m_os << "_" << coretype_name(n.m_datatype);
                break;
            case CORETYPE_I8:
            case CORETYPE_I16:
            case CORETYPE_I32:
            case CORETYPE_I64:
            case CORETYPE_I128:
            case CORETYPE_INT:
                m_os << n.m_value;
                m_os << "_" << coretype_name(n.m_datatype);
                break;
        }
    }

    virtual void visit(AST::ExprNodeFloat& n) override {
        m_expr_root = false;
        switch (n.m_datatype) {
            case CORETYPE_ANY:
                m_os.precision(::std::numeric_limits<double>::max_digits10 + 1);
                m_os << n.m_value;
                break;
            case CORETYPE_F16:
            case CORETYPE_F32:
                m_os.precision(::std::numeric_limits<float>::max_digits10 + 1);
                m_os << n.m_value;
                m_os << "_" << coretype_name(n.m_datatype);
                break;
            case CORETYPE_F64:
                m_os.precision(::std::numeric_limits<double>::max_digits10 + 1);
                m_os << n.m_value;
                m_os << "_" << coretype_name(n.m_datatype);
                break;
            case CORETYPE_F128:
                m_os.precision(::std::numeric_limits<double>::max_digits10 + 1);
                m_os << n.m_value;
                m_os << "_" << coretype_name(n.m_datatype);
                break;
            default:
                break;
        }
    }

    virtual void visit(AST::ExprNodeBool& n) override {
        m_expr_root = false;
        if (n.m_value) {
            m_os << "true";
        } else {
            m_os << "false";
        }
    }

    virtual void visit(AST::ExprNodeString& n) override {
        m_expr_root = false;
        m_os << "\"" << FmtEscaped(n.m_value) << "\"";
    }

    virtual void visit(AST::ExprNodeByteString& n) override {
        m_expr_root = false;
        m_os << "b\"" << FmtEscaped(n.m_value) << "\"";
    }

    virtual void visit(AST::ExprNodeCString& n) override {
        m_expr_root = false;
        m_os << "c\"" << FmtEscaped(n.m_value) << "\"";
    }

    virtual void visit(AST::ExprNodeStructLiteral& n) override {
        m_expr_root = false;
        m_os << n.m_path << " {\n";
        inc_indent();
        for (auto& i : n.m_values) {
            print_attrs(i.attrs);
            m_os << indent() << "r#" << i.name << ": ";
            AST::NodeVisitor::visit(i.value);
            m_os << ",\n";
        }
        if (n.m_base_value.get()) {
            m_os << indent() << ".. ";
            AST::NodeVisitor::visit(n.m_base_value);
            m_os << "\n";
        }
        m_os << indent() << "}";
        dec_indent();
    }

    virtual void visit(AST::ExprNodeStructLiteralPattern& n) override {
        m_expr_root = false;
        m_os << n.m_path << " {\n";
        inc_indent();
        for (auto& i : n.m_values) {
            print_attrs(i.attrs);
            m_os << indent() << "r#" << i.name << ": ";
            AST::NodeVisitor::visit(i.value);
            m_os << ",\n";
        }
        m_os << indent() << "..\n";
        m_os << indent() << "}";
        dec_indent();
    }

    virtual void visit(AST::ExprNodeArray& n) override {
        m_expr_root = false;
        m_os << "[";
        if (n.m_size.get()) {
            AST::NodeVisitor::visit(n.m_values[0]);
            m_os << "; ";
            AST::NodeVisitor::visit(n.m_size);
        } else {
            for (auto& item : n.m_values) {
                AST::NodeVisitor::visit(item);
                m_os << ", ";
            }
        }
        m_os << "]";
    }

    virtual void visit(AST::ExprNodeTuple& n) override {
        m_expr_root = false;
        m_os << "(";
        for (auto& item : n.m_values) {
            AST::NodeVisitor::visit(item);
            m_os << ", ";
        }
        m_os << ")";
    }

    virtual void visit(AST::ExprNodeNamedValue& n) override {
        m_expr_root = false;
        m_os << n.m_path;
    }

    virtual void visit(AST::ExprNodeField& n) override {
        m_expr_root = false;
        WRAPIF(n.m_obj, AST::ExprNodeDeref, AST::ExprNodeUniOp, AST::ExprNodeCast, AST::ExprNodeBinOp, AST::ExprNodeAssign, AST::ExprNodeMatch, AST::ExprNodeIf, AST::ExprNodeMatch);
        m_os << "." << n.m_name;
    }

    virtual void visit(AST::ExprNodeIndex& n) override {
        m_expr_root = false;
        WRAPIF(n.m_obj, AST::ExprNodeDeref, AST::ExprNodeUniOp, AST::ExprNodeCast, AST::ExprNodeBinOp, AST::ExprNodeAssign, AST::ExprNodeMatch, AST::ExprNodeIf, AST::ExprNodeMatch);
        m_os << "[";
        AST::NodeVisitor::visit(n.m_idx);
        m_os << "]";
    }

    virtual void visit(AST::ExprNodeDeref& n) override {
        m_expr_root = false;
        m_os << "*(";
        AST::NodeVisitor::visit(n.m_value);
        m_os << ")";
    }

    virtual void visit(AST::ExprNodeCast& n) override {
        m_expr_root = false;
        m_os << "(";
        AST::NodeVisitor::visit(n.m_value);
        m_os << ") as " << n.m_type;
    }

    virtual void visit(AST::ExprNodeTypeAnnotation& n) override {
        m_expr_root = false;
        m_os << "(";
        AST::NodeVisitor::visit(n.m_value);
        m_os << ") : " << n.m_type;
    }

    virtual void visit(AST::ExprNodeBinOp& n) override {
        m_expr_root = false;
        auto* left_binop = cast<AST::ExprNodeBinOp>(n.m_left.get());
        if (!n.m_left) {
            m_os << "/*null*/";
        } else if (left_binop && left_binop->m_type == n.m_type) {
            AST::NodeVisitor::visit(n.m_left);
        } else {
            WRAPIF(n.m_left, AST::ExprNodeCast, AST::ExprNodeBinOp);
        }
        m_os << " ";
        switch (n.m_type) {
            case AST::ExprNodeBinOp::CMPEQU:
                m_os << "==";
                break;
            case AST::ExprNodeBinOp::CMPNEQU:
                m_os << "!=";
                break;
            case AST::ExprNodeBinOp::CMPLT:
                m_os << "<";
                break;
            case AST::ExprNodeBinOp::CMPLTE:
                m_os << "<=";
                break;
            case AST::ExprNodeBinOp::CMPGT:
                m_os << ">";
                break;
            case AST::ExprNodeBinOp::CMPGTE:
                m_os << ">=";
                break;
            case AST::ExprNodeBinOp::BOOLAND:
                m_os << "&&";
                break;
            case AST::ExprNodeBinOp::BOOLOR:
                m_os << "||";
                break;
            case AST::ExprNodeBinOp::BITAND:
                m_os << "&";
                break;
            case AST::ExprNodeBinOp::BITOR:
                m_os << "|";
                break;
            case AST::ExprNodeBinOp::BITXOR:
                m_os << "^";
                break;
            case AST::ExprNodeBinOp::SHL:
                m_os << "<<";
                break;
            case AST::ExprNodeBinOp::SHR:
                m_os << ">>";
                break;
            case AST::ExprNodeBinOp::MULTIPLY:
                m_os << "*";
                break;
            case AST::ExprNodeBinOp::DIVIDE:
                m_os << "/";
                break;
            case AST::ExprNodeBinOp::MODULO:
                m_os << "%";
                break;
            case AST::ExprNodeBinOp::ADD:
                m_os << "+";
                break;
            case AST::ExprNodeBinOp::SUB:
                m_os << "-";
                break;
            case AST::ExprNodeBinOp::RANGE:
                m_os << "..";
                break;
            case AST::ExprNodeBinOp::RANGE_INC:
                m_os << "...";
                break;
            case AST::ExprNodeBinOp::PLACE_IN:
                m_os << "<-";
                break;
        }
        m_os << " ";
        auto* right_binop = cast<AST::ExprNodeBinOp>(n.m_right.get());
        if (!n.m_right) {
            m_os << "/*null*/";
        } else if (right_binop && right_binop->m_type != n.m_type) {
            paren_wrap(n.m_right);
        } else {
            AST::NodeVisitor::visit(n.m_right);
        }
    }

    virtual void visit(AST::ExprNodeUniOp& n) override {
        m_expr_root = false;
        switch (n.m_type) {
            case AST::ExprNodeUniOp::NEGATE:
                m_os << "-";
                break;
            case AST::ExprNodeUniOp::INVERT:
                m_os << "!";
                break;
            case AST::ExprNodeUniOp::BOX:
                m_os << "box ";
                break;
            case AST::ExprNodeUniOp::REF:
                m_os << "&";
                break;
            case AST::ExprNodeUniOp::REFMUT:
                m_os << "&mut ";
                break;
            case AST::ExprNodeUniOp::RawBorrow:
                m_os << "&raw const ";
                break;
            case AST::ExprNodeUniOp::RawBorrowMut:
                m_os << "&raw mut ";
                break;
            case AST::ExprNodeUniOp::QMARK:
                break;
            case AST::ExprNodeUniOp::AWait:
                break;
        }

        bool wrap = IS(*n.m_value, AST::ExprNodeBinOp) || IS(*n.m_value, AST::ExprNodeCast);
        if (wrap) {
            m_os << "(";
        }
        AST::NodeVisitor::visit(n.m_value);
        if (wrap) {
            m_os << ")";
        }
        switch (n.m_type) {
            case AST::ExprNodeUniOp::QMARK:
                m_os << "?";
                break;
            case AST::ExprNodeUniOp::AWait:
                m_os << ".await";
                break;
            default:
                break;
        }
    }

    virtual void visit(AST::ExprNodeMacroDefinition& n) override {
        m_os << "/* macro definition #" << n.m_definition_id << " */";
    }

private:
    void paren_wrap(::AST::ExprNodeP& node) {
        m_os << "(";
        AST::NodeVisitor::visit(node);
        m_os << ")";
    }

    void print_attrs(const AST::AttributeList& attrs);
    void print_params(const AST::GenericParams& params);
    void print_bounds(const AST::GenericParams& params);
    void print_pattern_tuple(const AST::Pattern::TuplePat& v, bool is_refutable);
    void print_pattern(const AST::Pattern& p, bool is_refutable);
    void print_type(const TypeRef& t);

    void inc_indent();
    RepeatLitStr indent();
    void dec_indent();
};

void RustPrinter::print_attrs(const AST::AttributeList& attrs) {
    for (const auto& a : attrs.m_items) {
        m_os << indent() << "#[" << a << "]\n";
    }
}

void RustPrinter::handle_module(const AST::Module& mod) {
    bool need_nl = true;

    for (const auto& ip : mod.m_items) {
        const auto& i = *ip;
        if (!i.data.is_Use()) {
            continue;
        }
        const auto& i_data = i.data.as_Use();
        //if(need_nl) {
        //    m_os << "\n";
        //    need_nl = false;
        //}
        if (i_data.entries.empty()) {
            continue;
        }
        m_os << indent() << i.vis << "use ";
        if (i_data.entries.size() > 1) {
            m_os << "{";
        }
        for (const auto& ent : i_data.entries) {
            if (&ent != &i_data.entries.front()) {
                m_os << ", ";
            }
            m_os << ent.path;
            if (ent.name == "") {
                m_os << "::*";
            } else if (ent.path.nodes().size() > 0 && ent.name != ent.path.nodes().back().name()) {
                m_os << " as " << ent.name;
            } else {
            }
        }
        if (i_data.entries.size() > 1) {
            m_os << "}";
        }
        m_os << ";\n";
    }
    need_nl = true;

    for (const auto& ip : mod.m_items) {
        const auto& item = *ip;
        if (!item.data.is_Crate()) {
            continue;
        }
        const auto& e = item.data.as_Crate();

        print_attrs(item.attrs);
        m_os << indent() << "extern crate \"" << e.name << "\" as " << item.name << ";\n";
    }

    for (const auto& ip : mod.m_items) {
        const auto& item = *ip;
        if (!item.data.is_ExternBlock()) {
            continue;
        }
        const auto& e = item.data.as_ExternBlock();

        print_attrs(item.attrs);
        m_os << indent() << "extern \"" << e.abi() << "\" {}\n";
    }

    for (const auto& ip : mod.m_items) {
        const auto& item = *ip;
        if (!item.data.is_Module()) {
            continue;
        }
        const auto& e = item.data.as_Module();

        m_os << "\n";
        m_os << indent() << item.vis << "mod " << item.name << "\n";
        m_os << indent() << "{\n";
        inc_indent();
        handle_module(e);
        dec_indent();
        m_os << indent() << "}\n";
        m_os << "\n";
    }

    for (const auto& ip : mod.m_items) {
        const auto& item = *ip;
        if (!item.data.is_Type()) {
            continue;
        }
        const auto& e = item.data.as_Type();

        if (need_nl) {
            m_os << "\n";
            need_nl = false;
        }
        print_attrs(item.attrs);
        m_os << indent() << item.vis << "type " << item.name;
        print_params(e.params());
        m_os << " = " << e.type();
        print_bounds(e.params());
        m_os << ";\n";
    }
    need_nl = true;

    for (const auto& ip : mod.m_items) {
        const auto& item = *ip;
        if (!item.data.is_Struct()) {
            continue;
        }
        const auto& e = item.data.as_Struct();

        m_os << "\n";
        print_attrs(item.attrs);
        m_os << indent() << item.vis << "struct " << item.name;
        handle_struct(e);
    }

    for (const auto& ip : mod.m_items) {
        const auto& item = *ip;
        if (!item.data.is_Enum()) {
            continue;
        }
        const auto& e = item.data.as_Enum();

        m_os << "\n";
        print_attrs(item.attrs);
        m_os << indent() << item.vis << "enum " << item.name;
        handle_enum(e);
    }

    for (const auto& ip : mod.m_items) {
        const auto& item = *ip;
        if (!item.data.is_Trait()) {
            continue;
        }
        const auto& e = item.data.as_Trait();

        m_os << "\n";
        print_attrs(item.attrs);
        m_os << indent() << item.vis << "trait " << item.name;
        handle_trait(e);
    }

    for (const auto& ip : mod.m_items) {
        const auto& item = *ip;
        if (!item.data.is_Static()) {
            continue;
        }
        const auto& e = item.data.as_Static();

        if (need_nl) {
            m_os << "\n";
            need_nl = false;
        }
        print_attrs(item.attrs);
        m_os << indent() << item.vis;
        switch (e.s_class()) {
            case AST::Static::CONST:
                m_os << "const ";
                break;
            case AST::Static::STATIC:
                m_os << "static ";
                break;
            case AST::Static::MUT:
                m_os << "static mut ";
                break;
        }
        m_os << item.name << ": " << e.type() << " = ";
        e.value().visit_nodes(*this);
        m_os << ";\n";
    }

    for (const auto& ip : mod.m_items) {
        const auto& item = *ip;
        if (!item.data.is_Function()) {
            continue;
        }
        const auto& e = item.data.as_Function();

        m_os << "\n";
        print_attrs(item.attrs);
        handle_function(item.vis, item.name, e);
    }

    for (const auto& ip : mod.m_items) {
        const auto& item = *ip;
        if (!item.data.is_Impl()) {
            continue;
        }
        const auto& i = item.data.as_Impl();

        m_os << "\n";
        m_os << indent() << "impl";
        if (i.def().is_const()) {
            m_os << " const";
        }
        print_params(i.def().params());
        if (i.def().trait().ent != AST::Path()) {
            m_os << " " << i.def().trait().ent << " for";
        }
        m_os << " " << i.def().type() << "\n";

        print_bounds(i.def().params());
        m_os << indent() << "{\n";
        inc_indent();
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
                    Static, m_os << indent(); switch (e.s_class()) {
                        case ::AST::Static::CONST:
                            m_os << "const ";
                            break;
                        case ::AST::Static::STATIC:
                            m_os << "static ";
                            break;
                        case ::AST::Static::MUT:
                            m_os << "static mut ";
                            break;
                    } m_os << it.name
                           << ": " << e.type() << " = ";
                    e.value().visit_nodes(*this);
                    m_os << ";\n";
                ),
                (Type, m_os << indent() << "type " << it.name << " = " << e.type() << ";\n";),
                (Function, handle_function(it.vis, it.name, e);)
            )
        }
        dec_indent();
        m_os << indent() << "}\n";
    }

}

void RustPrinter::print_params(const AST::GenericParams& params) {
    if (!params.m_params.empty()) {
        bool is_first = true;
        m_os << "<";
        for (const auto& p : params.m_params) {
            if (!is_first) {
                m_os << ", ";
            }
            TU_MATCH_HDRA( (p), {)
            TU_ARMA(None, p) {
                    m_os << "/*-*/";
                }
                TU_ARMA(Lifetime, p) {
                    m_os << p;
                }
                TU_ARMA(Type, p) {
                    m_os << p.attrs();
                    m_os << p.name();
                    if (!p.get_default().is_wildcard()) {
                        m_os << " = " << p.get_default();
                    }
                }
                TU_ARMA(Value, p) {
                    m_os << p.attrs();
                    m_os << "const " << p.name() << ": " << p.type();
                }
            }
            is_first = false;
        }
        m_os << ">";
    }
}

void RustPrinter::print_bounds(const AST::GenericParams& params) {
    if (!params.m_bounds.empty()) {
        inc_indent();
        bool is_first = true;

        for (const auto& b : params.m_bounds) {
            if (b.is_None()) {
                m_os << "/*-*/";
                continue;
            }
            if (!is_first) {
                m_os << ",\n";
            } else {
                m_os << indent() << "where\n";
            }
            is_first = false;

            m_os << indent();
            TU_MATCH(AST::GenericBound, (b), (ent), (None, m_os << "/*-*/";), (Lifetime, m_os << ent.test << ": " << ent.bound;), (TypeLifetime, m_os << ent.type << ": " << ent.bound;), (IsTrait, m_os << ent.outer_hrbs << ent.type << ": "; if (ent.constness == AST::BoundConstness::Always) m_os << "const "; else if (ent.constness == AST::BoundConstness::Maybe) m_os << "[const] "; m_os << ent.inner_hrbs << ent.trait;), (MaybeTrait, m_os << ent.type << ": ?" << ent.trait;), (NotTrait, m_os << ent.type << ": !" << ent.trait;), (Equality, m_os << ent.type << ": =" << ent.replacement;))
        }
        m_os << "\n";

        dec_indent();
    }
}

void RustPrinter::print_pattern_tuple(const AST::Pattern::TuplePat& v, bool is_refutable) {
    for (const auto& sp : v.start) {
        print_pattern(sp, is_refutable);
        m_os << ", ";
    }
    if (v.has_wildcard) {
        m_os << ".., ";
        for (const auto& sp : v.end) {
            print_pattern(sp, is_refutable);
            m_os << ", ";
        }
    }
}

void RustPrinter::print_pattern(const AST::Pattern& p, bool is_refutable) {
    for (const auto& pb : p.bindings()) {
        if (pb.m_mutable) {
            m_os << "mut ";
        }
        switch (pb.m_type) {
            case ::AST::PatternBinding::Type::MOVE:
                break;
            case ::AST::PatternBinding::Type::REF:
                m_os << "ref ";
                break;
            case ::AST::PatternBinding::Type::MUTREF:
                m_os << "ref mut ";
                break;
        }
        m_os << pb.m_name << "/*" << pb.m_slot << "*/";
        // If binding is irrefutable, and would be binding against a wildcard, just emit the name
        if (!is_refutable && p.bindings().size() == 1 && p.data().is_Any()) {
            return;
        }
        m_os << " @ ";
    }
    TU_MATCH(
        AST::Pattern::Data,
        (p.data()),
        (v),
        (Any, m_os << "_";),
        (MaybeBind, m_os << v.name << " /*?*/";),
        (Macro, m_os << *v.inv;),
        (Box,
         {
             const auto& v = p.data().as_Box();
             m_os << "box ";
             print_pattern(*v.sub, is_refutable);
         }),
        (Ref,
         {
             const auto& v = p.data().as_Ref();
             if (v.mut) {
                 m_os << "&mut ";
             } else {
                 m_os << "& ";
             }
             // Just in case the inner binds as mut
             m_os << "(";
             print_pattern(*v.sub, is_refutable);
             m_os << ")";
         }),
        (Value, m_os << v.start; if (!v.end.is_Invalid()) { m_os << " ..= " << v.end; }),
        (ValueLeftInc, m_os << v.start << " .. " << v.end;),
        (StructTuple, m_os << v.path << "("; this->print_pattern_tuple(v.tup_pat, is_refutable); m_os << ")";),
        (Struct,
         {
             const auto& v = p.data().as_Struct();
             m_os << v.path << "{";
             for (const auto& sp : v.sub_patterns) {
                 m_os << sp.name << ": ";
                 print_pattern(sp.pat, is_refutable);
                 m_os << ",";
             }
             if (!v.is_exhaustive) {
                 m_os << "..";
             }
             m_os << "}";
         }),
        (Tuple, m_os << "("; this->print_pattern_tuple(v, is_refutable); m_os << ")";),
        (
            Slice, m_os << "["; for (const auto& sp : v.sub_pats) {
                print_pattern(sp, is_refutable);
                m_os << ", ";
            } m_os << "]";
        ),
        (
            SplitSlice, m_os << "["; bool needs_comma = false; for (const auto& sp : v.leading) {
                print_pattern(sp, is_refutable);
                m_os << ", ";
            }

                                                               if (v.extra_bind.is_valid()) {
                                                                   const auto& b = v.extra_bind;
                                                                   if (b.m_mutable) {
                                                                       m_os << "mut ";
                                                                   }
                                                                   switch (b.m_type) {
                                                                       case ::AST::PatternBinding::Type::MOVE:
                                                                           break;
                                                                       case ::AST::PatternBinding::Type::REF:
                                                                           m_os << "ref ";
                                                                           break;
                                                                       case ::AST::PatternBinding::Type::MUTREF:
                                                                           m_os << "ref mut ";
                                                                           break;
                                                                   }
                                                                   m_os << b.m_name << "/*" << b.m_slot << "*/";
                                                               } m_os
                                                               << "..";
            needs_comma = true;

            if (v.trailing.size()) {
                if (needs_comma) {
                    m_os << ", ";
                }
                for (const auto& sp : v.trailing) {
                    print_pattern(sp, is_refutable);
                    m_os << ", ";
                }
            } m_os
            << "]";
        ),
        (Or, m_os << "("; for (const auto& e : v) {
            m_os << (&e == &v.front() ? "" : " | ");
            print_pattern(e, is_refutable);
        } m_os << ")";)
    )
}

void RustPrinter::print_type(const TypeRef& t) {
    m_os << t;
}

void RustPrinter::handle_struct(const AST::Struct& s) {
    print_params(s.params());

    TU_MATCH(
        AST::StructData,
        (s.m_data),
        (e),
        (Unit, m_os << " /* unit-like */\n"; print_bounds(s.params()); m_os << indent() << ";\n";),
        (Tuple, m_os << "("; for (const auto& i : e.ents) { m_os << i.m_vis << i.m_type << ", "; } m_os << ")\n"; print_bounds(s.params()); m_os << indent() << ";\n";),
        (Struct, m_os << "\n"; print_bounds(s.params());

         m_os << indent() << "{\n";
         inc_indent();
         for (const auto& i : e.ents) { m_os << indent() << i.m_vis << i.m_name << ": " << i.m_type.print_pretty() << ",\n"; } dec_indent();
         m_os << indent() << "}\n";)
    )
    m_os << "\n";
}

void RustPrinter::handle_enum(const AST::Enum& s) {
    print_params(s.params());
    m_os << "\n";
    print_bounds(s.params());

    m_os << indent() << "{\n";
    inc_indent();
    unsigned int idx = 0;
    for (const auto& i : s.variants()) {
        m_os << indent() << "/*" << idx << "*/" << i.m_name;
        TU_MATCH(AST::EnumVariantData, (i.m_data), (e), (Unit, ), (Tuple, m_os << "("; for (const auto& t : e.m_items) m_os << t.m_type.print_pretty() << ", "; m_os << ")";), (Struct, m_os << "{\n"; inc_indent(); for (const auto& i : e.m_fields) { m_os << indent() << i.m_name << ": " << i.m_type.print_pretty() << ",\n"; } dec_indent(); m_os << indent() << "}";))
        if (i.m_discriminant_value) {
            m_os << " = " << i.m_discriminant_value;
        }
        m_os << ",\n";
        idx++;
    }
    dec_indent();
    m_os << indent() << "}\n";
    m_os << "\n";
}

void RustPrinter::handle_trait(const AST::Trait& s) {
    print_params(s.params());
    {
        char c = ':';
        for (const auto& lft : s.lifetimes()) {
            m_os << " " << c << " " << lft.ent;
            c = '+';
        }
        for (const auto& t : s.supertraits()) {
            m_os << " " << c << " " << t.ent.hrbs << *t.ent.path;
            c = '+';
        }
    }
    m_os << "\n";
    print_bounds(s.params());

    m_os << indent() << "{\n";
    inc_indent();

    for (const auto& i : s.items()) {
        TU_MATCH_DEF(AST::Item, (i.data), (e), (), (Type, m_os << indent() << "type " << i.name << ";\n";), (Function, handle_function(AST::Visibility::make_bare_private(), i.name, e);))
    }

    dec_indent();
    m_os << indent() << "}\n";
    m_os << "\n";
}

void RustPrinter::handle_function(const AST::Visibility& vis, const RcString& name, const AST::Function& f) {
    m_os << indent();
    m_os << vis;
    if (f.is_const()) {
        m_os << "const ";
    }
    if (f.is_unsafe()) {
        m_os << "unsafe ";
    }
    if (f.is_async()) {
        m_os << "async ";
    }
    if (f.abi() != ABI_RUST) {
        m_os << "extern \"" << f.abi() << "\" ";
    }
    m_os << "fn " << name;
    print_params(f.params());
    m_os << "(";
    bool is_first = true;
    for (const auto& a : f.args()) {
        if (!is_first) {
            m_os << ", ";
        }
        print_attrs(a.attrs);
        print_pattern(a.pat, false);
        m_os << ": " << a.ty.print_pretty();
        is_first = false;
    }
    m_os << ")";
    if (!f.rettype().is_unit()) {
        m_os << " -> " << f.rettype().print_pretty();
    }

    if (f.code().is_valid()) {
        m_os << "\n";
        print_bounds(f.params());

        m_os << indent();
        f.code().visit_nodes(*this);
        m_os << "\n";
        //m_os << indent() << f.data.code() << "\n";
    } else {
        print_bounds(f.params());
        m_os << ";\n";
    }
}

void RustPrinter::inc_indent() {
    m_indent_level++;
}

RepeatLitStr RustPrinter::indent() {
    return RepeatLitStr{"    ", m_indent_level};
}

void RustPrinter::dec_indent() {
    m_indent_level--;
}

void Dump_Rust(const char* filename, const AST::Crate& crate) {
    ::std::ofstream os(filename);
    RustPrinter printer(os);
    printer.handle_module(crate.root_module());
}

void DumpAST_Node(::std::ostream& os, const AST::ExprNode& node) {
    RustPrinter printer(os);
    const_cast<AST::ExprNode&>(node).visit(printer);
}

#undef IS
#undef WRAPIF_CMD
#undef WRAPIF
