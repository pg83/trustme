#include "main_bindings.h"

#include "ast_crate.h"
#include "ast_ast.h"
#include "ast_expr.h"
#include "main_bindings.h"
#include "hir_hir.h" // ABI_RUST - TODO: Move elsewhere?
#include <fstream>
#include <limits> // std::numeric_limits

#include "cpp_unpack.h"

#define IS(v, c) (dynamic_cast<c*>(&v) != 0)
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

    virtual void visit(AST::ExprNode_Block& n) override {
        switch (n.m_block_type) {
            case AST::ExprNode_Block::Type::Bare:
                break;
            case AST::ExprNode_Block::Type::Unsafe:
                m_os << "unsafe ";
                break;
            case AST::ExprNode_Block::Type::Const:
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

    virtual void visit(AST::ExprNode_AsyncBlock& n) override {
        m_os << "async ";
        if (n.m_is_move) {
            m_os << "move ";
        }
        AST::NodeVisitor::visit(n.m_inner);
    }

    virtual void visit(AST::ExprNode_GeneratorBlock& n) override {
        m_os << "gen ";
        if (n.m_is_move) {
            m_os << "move ";
        }
        AST::NodeVisitor::visit(n.m_inner);
    }

    virtual void visit(AST::ExprNode_Try& n) override {
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

    virtual void visit(AST::ExprNode_Macro& n) override {
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

    virtual void visit(AST::ExprNode_Asm& n) override {
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

    virtual void visit(AST::ExprNode_Asm2& n) override {
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

    virtual void visit(AST::ExprNode_Flow& n) override {
        m_expr_root = false;
        switch (n.m_type) {
            case AST::ExprNode_Flow::RETURN:
                m_os << "return ";
                break;
            case AST::ExprNode_Flow::YIELD:
                m_os << "yield ";
                break;
            case AST::ExprNode_Flow::BREAK:
                m_os << "break ";
                break;
            case AST::ExprNode_Flow::CONTINUE:
                m_os << "continue ";
                break;
            case AST::ExprNode_Flow::YEET:
                m_os << "do yeet ";
                break;
        }
        if (n.m_target.name != "") {
            m_os << "'" << n.m_target << " ";
        }
        AST::NodeVisitor::visit(n.m_value);
    }

    virtual void visit(AST::ExprNode_LetBinding& n) override {
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

    virtual void visit(AST::ExprNode_Assign& n) override {
        m_expr_root = false;
        AST::NodeVisitor::visit(n.m_slot);
        switch (n.m_op) {
            case AST::ExprNode_Assign::NONE:
                m_os << "  = ";
                break;
            case AST::ExprNode_Assign::ADD:
                m_os << " += ";
                break;
            case AST::ExprNode_Assign::SUB:
                m_os << " -= ";
                break;
            case AST::ExprNode_Assign::MUL:
                m_os << " *= ";
                break;
            case AST::ExprNode_Assign::DIV:
                m_os << " /= ";
                break;
            case AST::ExprNode_Assign::MOD:
                m_os << " %= ";
                break;
            case AST::ExprNode_Assign::AND:
                m_os << " &= ";
                break;
            case AST::ExprNode_Assign::OR:
                m_os << " |= ";
                break;
            case AST::ExprNode_Assign::XOR:
                m_os << " ^= ";
                break;
            case AST::ExprNode_Assign::SHR:
                m_os << " >>= ";
                break;
            case AST::ExprNode_Assign::SHL:
                m_os << " <<= ";
                break;
        }
        AST::NodeVisitor::visit(n.m_value);
    }

    virtual void visit(AST::ExprNode_CallPath& n) override {
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

    virtual void visit(AST::ExprNode_CallMethod& n) override {
        m_expr_root = false;
        WRAPIF(n.m_val, AST::ExprNode_Deref, AST::ExprNode_UniOp, AST::ExprNode_Cast, AST::ExprNode_BinOp, AST::ExprNode_Assign, AST::ExprNode_Match, AST::ExprNode_If, AST::ExprNode_Match);
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

    virtual void visit(AST::ExprNode_CallObject& n) override {
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

    virtual void visit(AST::ExprNode_Loop& n) override {
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

    virtual void visit(AST::ExprNode_For& n) override {
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

    void visit(AST::ExprNode_While& n) override {
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

    virtual void visit(AST::ExprNode_Match& n) override {
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

    virtual void visit(AST::ExprNode_If& n) override {
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

            bool is_block = (dynamic_cast<const AST::ExprNode_Block*>(&*arm.m_body) != nullptr);
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
            bool is_block = (dynamic_cast<const AST::ExprNode_Block*>(&*n.m_else) != nullptr);
            if (!is_block) {
                m_os << "{ ";
            }
            AST::NodeVisitor::visit(n.m_else);
            if (!is_block) {
                m_os << " }";
            }
        }
    }

    virtual void visit(AST::ExprNode_Closure& n) override {
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

    virtual void visit(AST::ExprNode_WildcardPattern& n) override {
        m_os << "_";
    }

    virtual void visit(AST::ExprNode_Integer& n) override {
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

    virtual void visit(AST::ExprNode_Float& n) override {
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

    virtual void visit(AST::ExprNode_Bool& n) override {
        m_expr_root = false;
        if (n.m_value) {
            m_os << "true";
        } else {
            m_os << "false";
        }
    }

    virtual void visit(AST::ExprNode_String& n) override {
        m_expr_root = false;
        m_os << "\"" << FmtEscaped(n.m_value) << "\"";
    }

    virtual void visit(AST::ExprNode_ByteString& n) override {
        m_expr_root = false;
        m_os << "b\"" << FmtEscaped(n.m_value) << "\"";
    }

    virtual void visit(AST::ExprNode_CString& n) override {
        m_expr_root = false;
        m_os << "c\"" << FmtEscaped(n.m_value) << "\"";
    }

    virtual void visit(AST::ExprNode_StructLiteral& n) override {
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

    virtual void visit(AST::ExprNode_StructLiteralPattern& n) override {
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

    virtual void visit(AST::ExprNode_Array& n) override {
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

    virtual void visit(AST::ExprNode_Tuple& n) override {
        m_expr_root = false;
        m_os << "(";
        for (auto& item : n.m_values) {
            AST::NodeVisitor::visit(item);
            m_os << ", ";
        }
        m_os << ")";
    }

    virtual void visit(AST::ExprNode_NamedValue& n) override {
        m_expr_root = false;
        m_os << n.m_path;
    }

    virtual void visit(AST::ExprNode_Field& n) override {
        m_expr_root = false;
        WRAPIF(n.m_obj, AST::ExprNode_Deref, AST::ExprNode_UniOp, AST::ExprNode_Cast, AST::ExprNode_BinOp, AST::ExprNode_Assign, AST::ExprNode_Match, AST::ExprNode_If, AST::ExprNode_Match);
        m_os << "." << n.m_name;
    }

    virtual void visit(AST::ExprNode_Index& n) override {
        m_expr_root = false;
        WRAPIF(n.m_obj, AST::ExprNode_Deref, AST::ExprNode_UniOp, AST::ExprNode_Cast, AST::ExprNode_BinOp, AST::ExprNode_Assign, AST::ExprNode_Match, AST::ExprNode_If, AST::ExprNode_Match);
        m_os << "[";
        AST::NodeVisitor::visit(n.m_idx);
        m_os << "]";
    }

    virtual void visit(AST::ExprNode_Deref& n) override {
        m_expr_root = false;
        m_os << "*(";
        AST::NodeVisitor::visit(n.m_value);
        m_os << ")";
    }

    virtual void visit(AST::ExprNode_Cast& n) override {
        m_expr_root = false;
        m_os << "(";
        AST::NodeVisitor::visit(n.m_value);
        m_os << ") as " << n.m_type;
    }

    virtual void visit(AST::ExprNode_TypeAnnotation& n) override {
        m_expr_root = false;
        m_os << "(";
        AST::NodeVisitor::visit(n.m_value);
        m_os << ") : " << n.m_type;
    }

    virtual void visit(AST::ExprNode_BinOp& n) override {
        m_expr_root = false;
        if (!n.m_left) {
            m_os << "/*null*/";
        } else if (IS(*n.m_left, AST::ExprNode_BinOp) && dynamic_cast<AST::ExprNode_BinOp&>(*n.m_left).m_type == n.m_type) {
            AST::NodeVisitor::visit(n.m_left);
        } else {
            WRAPIF(n.m_left, AST::ExprNode_Cast, AST::ExprNode_BinOp);
        }
        m_os << " ";
        switch (n.m_type) {
            case AST::ExprNode_BinOp::CMPEQU:
                m_os << "==";
                break;
            case AST::ExprNode_BinOp::CMPNEQU:
                m_os << "!=";
                break;
            case AST::ExprNode_BinOp::CMPLT:
                m_os << "<";
                break;
            case AST::ExprNode_BinOp::CMPLTE:
                m_os << "<=";
                break;
            case AST::ExprNode_BinOp::CMPGT:
                m_os << ">";
                break;
            case AST::ExprNode_BinOp::CMPGTE:
                m_os << ">=";
                break;
            case AST::ExprNode_BinOp::BOOLAND:
                m_os << "&&";
                break;
            case AST::ExprNode_BinOp::BOOLOR:
                m_os << "||";
                break;
            case AST::ExprNode_BinOp::BITAND:
                m_os << "&";
                break;
            case AST::ExprNode_BinOp::BITOR:
                m_os << "|";
                break;
            case AST::ExprNode_BinOp::BITXOR:
                m_os << "^";
                break;
            case AST::ExprNode_BinOp::SHL:
                m_os << "<<";
                break;
            case AST::ExprNode_BinOp::SHR:
                m_os << ">>";
                break;
            case AST::ExprNode_BinOp::MULTIPLY:
                m_os << "*";
                break;
            case AST::ExprNode_BinOp::DIVIDE:
                m_os << "/";
                break;
            case AST::ExprNode_BinOp::MODULO:
                m_os << "%";
                break;
            case AST::ExprNode_BinOp::ADD:
                m_os << "+";
                break;
            case AST::ExprNode_BinOp::SUB:
                m_os << "-";
                break;
            case AST::ExprNode_BinOp::RANGE:
                m_os << "..";
                break;
            case AST::ExprNode_BinOp::RANGE_INC:
                m_os << "...";
                break;
            case AST::ExprNode_BinOp::PLACE_IN:
                m_os << "<-";
                break;
        }
        m_os << " ";
        if (!n.m_right) {
            m_os << "/*null*/";
        } else if (IS(*n.m_right, AST::ExprNode_BinOp) && dynamic_cast<AST::ExprNode_BinOp&>(*n.m_right).m_type != n.m_type) {
            paren_wrap(n.m_right);
        } else {
            AST::NodeVisitor::visit(n.m_right);
        }
    }

    virtual void visit(AST::ExprNode_UniOp& n) override {
        m_expr_root = false;
        switch (n.m_type) {
            case AST::ExprNode_UniOp::NEGATE:
                m_os << "-";
                break;
            case AST::ExprNode_UniOp::INVERT:
                m_os << "!";
                break;
            case AST::ExprNode_UniOp::BOX:
                m_os << "box ";
                break;
            case AST::ExprNode_UniOp::REF:
                m_os << "&";
                break;
            case AST::ExprNode_UniOp::REFMUT:
                m_os << "&mut ";
                break;
            case AST::ExprNode_UniOp::RawBorrow:
                m_os << "&raw const ";
                break;
            case AST::ExprNode_UniOp::RawBorrowMut:
                m_os << "&raw mut ";
                break;
            case AST::ExprNode_UniOp::QMARK:
                break;
            case AST::ExprNode_UniOp::AWait:
                break;
        }

        bool wrap = IS(*n.m_value, AST::ExprNode_BinOp) || IS(*n.m_value, AST::ExprNode_Cast);
        if (wrap) {
            m_os << "(";
        }
        AST::NodeVisitor::visit(n.m_value);
        if (wrap) {
            m_os << ")";
        }
        switch (n.m_type) {
            case AST::ExprNode_UniOp::QMARK:
                m_os << "?";
                break;
            case AST::ExprNode_UniOp::AWait:
                m_os << ".await";
                break;
            default:
                break;
        }
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

// HACK: Assume that anon modules have been printed already, so don't include them here.
// - Needed, because this code is used for proc macro output, which doen't like the `#<n>` syntax
#if 0
    for(const auto& m : mod.anon_mods())
    {
        if(!m) {
            m_os << indent() << "/* mod ? (delted anon) */\n";
            continue ;
        }
        m_os << indent() << "mod " << m->path().nodes.back() << " {\n";
        inc_indent();
        handle_module(*m);
        dec_indent();
        m_os << indent() << "}\n";
    }
#endif
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
            TU_MATCH(AST::GenericBound, (b), (ent), (None, m_os << "/*-*/";), (Lifetime, m_os << ent.test << ": " << ent.bound;), (TypeLifetime, m_os << ent.type << ": " << ent.bound;), (IsTrait, m_os << ent.outer_hrbs << ent.type << ": " << ent.inner_hrbs << ent.trait;), (MaybeTrait, m_os << ent.type << ": ?" << ent.trait;), (NotTrait, m_os << ent.type << ": !" << ent.trait;), (Equality, m_os << ent.type << ": =" << ent.replacement;))
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

#include "ast_ast.h"
#include "ast_expr.h"
#include "ast_crate.h"
#include "main_bindings.h"
#include "hir_hir.h" // ABI_RUST

#define NEWNODE(_ty, ...) ::AST::ExprNodeP(new ::AST::ExprNode##_ty(__VA_ARGS__))

void Expand_TestHarness(::AST::Crate& crate) {
    ASSERT_BUG(Span(), crate.m_ext_cratename_test != "", "Crate `test` not loaded");
    ASSERT_BUG(Span(), crate.m_ext_cratename_std != "", "Crate `std` not loaded");
    auto c_test = crate.m_ext_cratename_test;
    // Create the following module:
    // ```
    // mod `#test` {
    //   extern crate std;
    //   extern crate test;
    //   fn main() {
    //     self::test::test_main_static(&::`#test`::TESTS);
    //   }
    //   static TESTS: [test::TestDescAndFn; _] = [
    //     test::TestDescAndFn { desc: test::TestDesc { name: "foo", ignore: false, should_panic: test::ShouldPanic::No }, testfn: ::path::to::foo },
    //     ];
    // }
    // ```

    // ---- main function ----
    auto main_fn = ::AST::Function{Span(), TypeRef(TypeRef::TagUnit(), Span()), {}};
    {
        auto call_node = NEWNODE(_CallPath, ::AST::Path(c_test, {::AST::PathNode("test_main_static")}), ::make_vec1(NEWNODE(_UniOp, ::AST::ExprNode_UniOp::REF, NEWNODE(_NamedValue, ::AST::Path("", {::AST::PathNode("test#"), ::AST::PathNode("TESTS")})))));
        main_fn.set_code(mv$(call_node));
    }

    // ---- test list ----
    ::std::vector<::AST::ExprNodeP> test_nodes;

    for (const auto& test : crate.m_tests) {
        ::AST::ExprNode_StructLiteral::t_values desc_vals;
        // `name: "foo",`
        desc_vals.push_back({{}, "name", NEWNODE(_CallPath, ::AST::Path(c_test, {::AST::PathNode("StaticTestName")}), ::make_vec1(NEWNODE(_String, test.name)))});
        // `ignore: false,`
        desc_vals.push_back({{}, "ignore", NEWNODE(_Bool, test.ignore)});
        // `should_panic: ShouldPanic::No,`
        {
            ::AST::ExprNodeP should_panic_val;
            switch (test.panic_type) {
                case ::AST::TestDesc::ShouldPanic::No:
                    should_panic_val = NEWNODE(_NamedValue, ::AST::Path(c_test, {::AST::PathNode("ShouldPanic"), ::AST::PathNode("No")}));
                    break;
                case ::AST::TestDesc::ShouldPanic::Yes:
                    should_panic_val = NEWNODE(_NamedValue, ::AST::Path(c_test, {::AST::PathNode("ShouldPanic"), ::AST::PathNode("Yes")}));
                    break;
                case ::AST::TestDesc::ShouldPanic::YesWithMessage:
                    should_panic_val = NEWNODE(_CallPath, ::AST::Path(c_test, {::AST::PathNode("ShouldPanic"), ::AST::PathNode("YesWithMessage")}), make_vec1(NEWNODE(_String, test.expected_panic_message)));
                    break;
            }
            desc_vals.push_back({{}, "should_panic", mv$(should_panic_val)});
        }
        {
            // TODO: Get this from attributes
            desc_vals.push_back({{}, "compile_fail", NEWNODE(_Bool, false)});
            desc_vals.push_back({{}, "no_run", NEWNODE(_Bool, false)});
            desc_vals.push_back({{}, "test_type", NEWNODE(_NamedValue, ::AST::Path(c_test, {AST::PathNode("TestType"), AST::PathNode("UnitTest")}))});
        }
        {
            desc_vals.push_back({{}, "ignore_message", NEWNODE(_NamedValue, ::AST::Path(crate.m_ext_cratename_std, {AST::PathNode("option"), AST::PathNode("Option"), AST::PathNode("None")}))});
            auto sp = test.span.get_top_file_span();
            desc_vals.push_back({{}, "source_file", NEWNODE(_String, sp.filename.c_str())});
            desc_vals.push_back({{}, "start_line", NEWNODE(_Integer, U128(sp.start_line), CORETYPE_UINT)});
            desc_vals.push_back({{}, "start_col", NEWNODE(_Integer, U128(sp.start_ofs), CORETYPE_UINT)});
            desc_vals.push_back({{}, "end_line", NEWNODE(_Integer, U128(sp.end_line), CORETYPE_UINT)});
            desc_vals.push_back({{}, "end_col", NEWNODE(_Integer, U128(sp.end_ofs), CORETYPE_UINT)});
        }
        auto desc_expr = NEWNODE(_StructLiteral, ::AST::Path(c_test, {::AST::PathNode("TestDesc")}), nullptr, mv$(desc_vals));

        ::AST::ExprNode_StructLiteral::t_values descandfn_vals;
        descandfn_vals.push_back({{}, RcString::new_interned("desc"), mv$(desc_expr)});

        auto test_fcn_node = NEWNODE(_NamedValue, AST::Path(test.path));
        {
            // Convert `fn()` into `fn()->Result<(),String>`
            // Use `|| ::test::assert_test_result( fcn() )`
            test_fcn_node = NEWNODE(_Closure, {}, TypeRef(Span()), NEWNODE(_CallPath, ::AST::Path(c_test, {::AST::PathNode("assert_test_result")}), ::make_vec1(NEWNODE(_CallPath, AST::Path(test.path), {}))), false, false);
        }
        auto test_type_var_name = test.is_benchmark ? "StaticBenchFn" : "StaticTestFn";
        descandfn_vals.push_back({{}, RcString::new_interned("testfn"), NEWNODE(_CallPath, ::AST::Path(c_test, {::AST::PathNode(test_type_var_name)}), ::make_vec1(std::move(test_fcn_node)))});

        test_nodes.push_back(NEWNODE(_StructLiteral, ::AST::Path(c_test, {::AST::PathNode("TestDescAndFn")}), nullptr, mv$(descandfn_vals)));
        // NOTE: 1.39+ needs &TestDescAndFn here
        {
            test_nodes.back() = NEWNODE(_UniOp, ::AST::ExprNode_UniOp::REF, mv$(test_nodes.back()));
        }
    }
    auto* tests_array = new ::AST::ExprNode_Array(mv$(test_nodes));

    size_t test_count = tests_array->m_values.size();
    auto list_item_ty = TypeRef(Span(), ::AST::Path(c_test, {::AST::PathNode("TestDescAndFn")}));
    // NOTE: 1.39+ needs &TestDescAndFn here
    {
        list_item_ty = TypeRef(TypeRef::TagReference(), Span(), AST::LifetimeRef::new_static(), false, mv$(list_item_ty));
    }
    auto tests_list = ::AST::Static{::AST::Static::Class::STATIC, TypeRef(TypeRef::TagSizedArray(), Span(), mv$(list_item_ty), ::std::shared_ptr<::AST::ExprNode>(new ::AST::ExprNode_Integer(U128(test_count), CORETYPE_UINT))), ::AST::Expr(mv$(tests_array))};

    // ---- module ----
    auto newmod = ::AST::Module{::AST::AbsolutePath("", {"test#"})};
    auto vis_private = AST::Visibility::make_restricted(AST::Visibility::Ty::Private, newmod.path());
    // - TODO: These need to be loaded too.
    //  > They don't actually need to exist here, just be loaded (and use absolute paths)
    //newmod.add_ext_crate(Span(), false, "std", "std", {});
    //newmod.add_ext_crate(Span(), false, "test", "test", {});

    newmod.add_item(Span(), vis_private, "main", mv$(main_fn), {});
    newmod.add_item(Span(), vis_private, "TESTS", mv$(tests_list), {});

    crate.m_root_module.add_item(Span(), vis_private, "test#", mv$(newmod), {});
    crate.m_lang_items["mrustc-main"] = ::AST::AbsolutePath("", {"test#", "main"});
}

#undef NEWNODE

#include <iostream>
#include <iomanip>
#include <string>
#include <set>
#include <climits>
#include "version.h"
#include "string_view.h"
#include "parse_lex.h"
#include "parse_parseerror.h"
#include "parse_common.h" // For edition checks
#include "ast_ast.h"
#include "ast_crate.h"
#include <cstring>
#include "main_bindings.h"
#include "resolve_main_bindings.h"
#include "hir_main_bindings.h"
#include "hir_conv_main_bindings.h"
#include "hir_typeck_main_bindings.h"
#include "hir_expand_main_bindings.h"
#include "mir_main_bindings.h"
#include "trans_main_bindings.h"
#include "trans_target.h"
#include "trait_solver_mode.h"

#include "expand_cfg.h"
#include "target_detect.h" // tools/common/target_detect.h
#include "debug_inner.h"
#include "memory_dump.h"
#include <std/mem/obj_pool.h>

#ifndef __has_feature
    #define __has_feature(x) 0
#endif

#if __has_feature(address_sanitizer) || __has_feature(undefined_behavior_sanitizer)
    #define MRUSTC_SANITIZER_BUILD 1
#else
    #define MRUSTC_SANITIZER_BUILD 0
#endif

TraitSolverConfig gTraitSolverConfig;

struct ProgramParams {
    enum eLastStage {
        STAGE_PARSE,
        STAGE_EXPAND,
        STAGE_RESOLVE,
        STAGE_TYPECK,
        STAGE_BORROWCK,
        STAGE_MIR,
        STAGE_ALL,
    } last_stage = STAGE_ALL;

    ::std::string infile;
    ::std::string outfile;
    ::std::string output_dir = "";
    ::std::string target = DEFAULT_TARGET_NAME;

    ::std::string emit_depfile;

    AST::Edition edition = AST::Edition::Rust2015;
    ::AST::Crate::Type crate_type = ::AST::Crate::Type::Unknown;
    ::std::string crate_name;
    ::std::string crate_name_suffix;

    unsigned opt_level = 0;
    // rustc defaults MIR optimisation to 1 at -O0 and to 2 otherwise.
    // Keep the explicit bit separate so `-Zmir-opt-level=0` is distinguishable
    // from the implicit default.
    unsigned mir_opt_level = 0;
    bool mir_opt_level_explicit = false;
    bool emit_debug_info = false;

    bool test_harness = false;

    // NOTE: If populated, nothing happens except for loading the target
    ::std::string target_saveback;
    // NOTE: if true, no parse/compilation performed (target is loaded though)
    bool print_cfgs = false;

    //
    bool run_borrowcheck = false;

    TraitSolverConfig trait_solver;

    ::std::vector<const char*> lib_search_dirs;
    ::std::vector<const char*> libraries;
    ::std::map<::std::string, ::std::string> crate_overrides; // --extern name=path

    ::std::set<::std::string> features;

    struct {
        /// Testing hack: Pause just after startup (to allow a debugger to attach)
        bool pause = false;

        bool full_validate = false;
        bool full_validate_early = false;

        bool dump_ast = false;
        bool dump_hir = false;
        bool dump_mir = false;
    } debug;

    struct {
        ::std::string codegen_type;
        ::std::string emit_build_command;
        ::std::string panic_type;
    } codegen;

    ProgramParams(int argc, char* argv[]);

    unsigned effective_mir_opt_level() const {
        return mir_opt_level_explicit ? mir_opt_level : (opt_level == 0 ? 1 : 2);
    }
    bool enable_mir_inlining() const {
        const auto level = effective_mir_opt_level();
        return level >= 3 || (level == 2 && opt_level >= 2);
    }

    void show_help() const;
};

template <typename Rv, typename Fcn>
Rv CompilePhase(const char* name, Fcn f) {
    DebugTimedPhase timed_phase(name);
    return f();
}

template <typename Fcn>
void CompilePhaseV(const char* name, Fcn f) {
    DebugTimedPhase timed_phase(name);
    f();
}

void init_debug_list() {
    debug_init_phases(
        "MRUSTC_DEBUG",
        {"Target Load",
         "Parse",
         "LoadCrates",
         "Expand",
         "Dump Expanded",
         "Implicit Crates",

         "Resolve Use",
         "Resolve Index",
         "Resolve Absolute",

         "HIR Lower",

         "Lifetime Elision",
         "Resolve Type Aliases",
         "Resolve Bind",
         "Resolve UFCS Outer",
         "Resolve UFCS paths",
         "Resolve HIR Self Type",
         "Resolve HIR Markings",
         "Sort Impls",
         "Constant Evaluate",

         "Typecheck Outer",
         "Typecheck Expressions",

         "Expand HIR Annotate",
         "Expand HIR Static Borrow Mark",
         "Expand HIR Lifetimes",
         "Expand HIR Closures",
         "Expand HIR Static Borrow",
         "Expand HIR Calls",
         "Expand HIR VTables",
         "Expand HIR Reborrows",
         "Expand HIR ErasedType",
         "Typecheck Expressions (validate)",
         "Expand HIR Lifetimes (validate)",

         "Dump HIR",
         "Lower MIR",
         "MIR Validate Full Early",
         "Dump MIR",
         "Constant Evaluate Full",
         "MIR Cleanup",
         "MIR Borrowcheck",
         "MIR Optimise",
         "MIR Validate PO",
         "MIR Validate Full",

         "HIR Serialise",
         "Trans Enumerate",
         "Trans Auto Impls",
         "Trans Monomorph",
         "MIR Optimise Inline",
         "MIR Cleanup 2",
         "MIR Optimise Inline PostSave",
         "Trans Enumerate Cleanup",
         "Trans Codegen"}
    );
}

/// main!
int main(int argc, char* argv[]) {
    init_debug_list();
    ProgramParams params(argc, argv);
    gTraitSolverConfig = params.trait_solver;
    const auto mir_opt_level = params.effective_mir_opt_level();
    const auto enable_mir_inlining = params.enable_mir_inlining();
    if (params.codegen.panic_type.empty()) {
        params.codegen.panic_type = "unwind";
    }

    if (params.debug.pause) {
        char c;
        ::std::cerr << "Pausing to attach a debugger\nType any text to continue" << std::endl;
        ::std::cin >> c;
    }

    // Set up cfg values
    CompilePhaseV("Setup", [&]() {
        Cfg_SetValue("rust_compiler", "mrustc");
        Cfg_SetValue("panic", params.codegen.panic_type);
        Cfg_SetValueCb("feature", [&params](const ::std::string& s) {
            return params.features.count(s) != 0;
        });
#if 0
        DEBUG("sizeof(AST::TypeRef) = " << sizeof(TypeRef));
        DEBUG("sizeof(AST::Item) = " << sizeof(AST::Item));
        DEBUG("sizeof(AST::Impl) = " << sizeof(AST::Impl));
        DEBUG("sizeof(AST::Function) = " << sizeof(AST::Function));
        DEBUG("sizeof(AST::Module) = " << sizeof(AST::Module));
        DEBUG("sizeof(HIR::TypeRef) = " << sizeof(HIR::TypeRef));
        DEBUG("sizeof(HIR::Path) = " << sizeof(HIR::Path));
#endif
    });
    CompilePhaseV("Target Load", [&]() {
        Target_SetCfg(params.target);
    });

    if (params.print_cfgs) {
        Cfg_Dump(std::cout);
        return 0;
    }
    if (params.target_saveback != "") {
        Target_ExportCurSpec(params.target_saveback);
        return 0;
    }

    if (params.infile == "") {
        ::std::cerr << "No input file passed" << ::std::endl;
        return 1;
    }

    if (params.test_harness) {
        Cfg_SetFlag("test");
    }

    Expand_Init();
#if MRUSTC_SANITIZER_BUILD
    // Keep teardown out of production, but make sanitizer builds destroy every
    // pooled object so ASan/LSan can distinguish real leaks from arena lifetime.
    auto pool_owner = stl::ObjPool::fromMemory();
    auto* pool = pool_owner.mutPtr();
#else
    auto* pool = stl::ObjPool::fromMemoryRaw();
#endif
    auto* types = pool->make<HIR::TypeInterner>(*pool);

    try {
        // Parse the crate into AST
        AST::Crate* crate_ptr = CompilePhase<AST::Crate*>("Parse", [&]() {
            return Parse_Crate(pool, *types, params.infile, params.edition);
        });
        AST::Crate& crate = *crate_ptr;
        crate.m_test_harness = params.test_harness;
        crate.m_crate_name_suffix = params.crate_name_suffix;
        //crate.m_crate_name = params.crate_name;

        if (params.last_stage == ProgramParams::STAGE_PARSE) {
            return 0;
        }
        memory_dump("Parsed");

        // Load external crates.
        CompilePhaseV("LoadCrates", [&]() {
            // Hacky!
            AST::g_crate_overrides = params.crate_overrides;
            for (const auto& ld : params.lib_search_dirs) {
                AST::g_crate_load_dirs.push_back(ld);
            }
            crate.load_externs();
            if (params.test_harness) {
                auto test_crate_name = RcString::new_interned("test");
                AST::g_implicit_crates.insert(std::make_pair(test_crate_name, crate.load_extern_crate(Span(), test_crate_name)));
            }
        });

        if (params.crate_name != "") {
            // Extract the crate type and name from the crate attributes
            auto crate_type = params.crate_type;
            if (crate_type == ::AST::Crate::Type::Unknown) {
                crate_type = crate.m_crate_type;
            }
            if (crate_type == ::AST::Crate::Type::Unknown) {
                // Assume to be executable
                crate_type = ::AST::Crate::Type::Executable;
            }
            crate.m_crate_type = crate_type;

            crate.set_crate_name(params.crate_name);
            crate.m_crate_type = ::AST::Crate::Type::Unknown;
        }

        // Iterate all items in the AST, applying syntax extensions
        CompilePhaseV("Expand", [&]() {
            Expand(crate);

            if (params.test_harness) {
                Expand_TestHarness(crate);
            }
        });

        // Extract the crate type and name from the crate attributes
        auto crate_type = params.crate_type;
        if (crate_type == ::AST::Crate::Type::Unknown) {
            crate_type = crate.m_crate_type;
        }
        if (crate_type == ::AST::Crate::Type::Unknown) {
            // Assume to be executable
            crate_type = ::AST::Crate::Type::Executable;
        }
        crate.m_crate_type = crate_type;

        if (crate.m_crate_type == ::AST::Crate::Type::ProcMacro) {
            Expand_ProcMacro(crate);
        }

        auto crate_name = params.crate_name;
        if (crate_name == "") {
            crate_name = crate.m_crate_name_set;
        }
        if (crate_name == "") {
            auto s = params.infile.find_last_of('/');
            if (s == ::std::string::npos) {
                s = 0;
            } else {
                s += 1;
            }
            auto s2 = params.infile.find_last_of('\\');
            if (s2 == ::std::string::npos) {
                s2 = 0;
            } else {
                s2 += 1;
            }
            s = std::max(s, s2);
            auto e = params.infile.find_first_of('.', s);
            if (e == ::std::string::npos) {
                e = params.infile.size() - s;
            }

            crate_name = ::std::string(params.infile.begin() + s, params.infile.begin() + e);
            for (auto& b : crate_name) {
                if ('0' <= b && b <= '9') {
                } else if ('A' <= b && b <= 'Z') {
                } else if (b == '_') {
                } else if (b == '-') {
                    b = '_';
                } else {
                    // TODO: Error?
                }
            }
        }
        if (params.test_harness) {
            crate_name += "$test";
        }
        crate.set_crate_name(crate_name);

        if (params.outfile == "") {
#ifdef WIN32
    #define EXESUF ".exe"
#else
    #define EXESUF ""
#endif
            switch (crate.m_crate_type) {
                case ::AST::Crate::Type::RustLib:
                    params.outfile = FMT(params.output_dir << "lib" << crate.m_crate_name_set << ".rlib");
                    break;
                case ::AST::Crate::Type::Executable:
                    params.outfile = FMT(params.output_dir << crate.m_crate_name_set << EXESUF);
                    break;
                case ::AST::Crate::Type::ProcMacro:
                    params.outfile = FMT(params.output_dir << "lib" << crate.m_crate_name_set << "-plugin" EXESUF);
                    break;
                default:
                    params.outfile = FMT(params.output_dir << crate.m_crate_name_set << ".o");
                    break;
            }
            DEBUG("params.outfile = " << params.outfile);
        }

        if (params.debug.dump_ast) {
            CompilePhaseV("Dump Expanded", [&]() {
                Dump_Rust(FMT(params.outfile << "_1_ast.rs").c_str(), crate);
            });
        }

        if (params.last_stage == ProgramParams::STAGE_EXPAND) {
            return 0;
        }
        memory_dump("Expanded");

        // Allocator and panic strategies
        CompilePhaseV("Implicit Crates", [&]() {
            if (crate.m_crate_type == ::AST::Crate::Type::Executable || params.test_harness || crate.m_crate_type == ::AST::Crate::Type::ProcMacro) {
                bool allocator_crate_loaded = false;
                RcString alloc_crate_name;
                bool panic_runtime_loaded = false;
                RcString panic_crate_name;
                bool panic_runtime_needed = false;
                for (const auto& ec : crate.m_extern_crates) {
                    ::std::ostringstream ss;
                    for (const auto& e : ec.second.m_hir->m_lang_items) {
                        ss << e << ",";
                    }
                    DEBUG("Looking at lang items from " << ec.first << " : " << ss.str());
                    if (ec.second.m_hir->m_lang_items.count("mrustc-allocator")) {
                        if (allocator_crate_loaded) {
                            ERROR(Span(), E0000, "Multiple allocator crates loaded - " << alloc_crate_name << " and " << ec.first);
                        }
                        alloc_crate_name = ec.first;
                        allocator_crate_loaded = true;
                    }
                    if (ec.second.m_hir->m_lang_items.count("mrustc-panic_runtime")) {
                        if (panic_runtime_loaded) {
                            //ERROR(Span(), E0000, "Multiple panic_runtime crates loaded - " << panic_crate_name << " and " << ec.first);
                            WARNING(Span(), W0000, "Multiple panic_runtime crates loaded - " << panic_crate_name << " and " << ec.first);
                        } else {
                            panic_crate_name = ec.first;
                            panic_runtime_loaded = true;
                        }
                    }
                    if (ec.second.m_hir->m_lang_items.count("mrustc-needs_panic_runtime")) {
                        panic_runtime_needed = true;
                    }
                }
                // The default (system) allocator is provided by liballoc.
                allocator_crate_loaded = true;
                if (!allocator_crate_loaded) {
                    crate.load_extern_crate(Span(), "alloc_system");
                }

                if (panic_runtime_needed /*&& !panic_runtime_loaded*/) {
                    auto panic_crate = "panic_" + params.codegen.panic_type;
                    crate.load_extern_crate(Span(), panic_crate.c_str());
                }

                // - `mrustc-main` lang item default
                crate.m_lang_items.insert(::std::make_pair(::std::string("mrustc-main"), ::AST::AbsolutePath("", {"main"})));
            }
        });

        /// Emit the dependency files
        if (params.emit_depfile != "") {
            // - Iterate all loaded files for modules
            struct PathEnumerator {
                ::std::vector<::std::string> out;

                void visit_module(::AST::Module& mod) {
                    if (mod.m_file_info.path != "!" && mod.m_file_info.path.back() != '/') {
                        out.push_back(mod.m_file_info.path);
                    }
                    // TODO: Should we check anon modules?
                    //for(auto& amod : mod.anon_mods()) {
                    //    this->visit_module(*amod);
                    //}
                    for (auto& i : mod.m_items) {
                        if (i->data.is_Module()) {
                            this->visit_module(i->data.as_Module());
                        }
                    }
                }
            };

            PathEnumerator pe;
            pe.visit_module(crate.m_root_module);

            ::std::ofstream of{params.emit_depfile};
            // TODO: Escape spaces and colons in these paths
            of << params.outfile << ": " << params.infile;
            for (const auto& mod_path : pe.out) {
                of << " " << mod_path;
            }
            of << ::std::endl;

            of << params.outfile << ":";
            // - Iterate all loaded crates files
            for (const auto& ec : crate.m_extern_crates) {
                of << " " << ec.second.m_filename;
            }
            // - Iterate all extra files (include! and friends)
        }

        // Resolve names to be absolute names (include references to the relevant struct/global/function)
        // - This does name checking on types and free functions.
        // - Resolves all identifiers/paths to references
        CompilePhaseV("Resolve Use", [&]() {
            Resolve_Use(crate); // - Absolutise and resolve use statements
        });
        CompilePhaseV("Resolve Index", [&]() {
            Resolve_Index(crate); // - Build up a per-module index of avalable names (faster and simpler later resolve)
        });
        CompilePhaseV("Resolve Absolute", [&]() {
            Resolve_Absolutise(crate); // - Convert all paths to Absolute or UFCS, and resolve variables
        });
        memory_dump("Resolved");

        if (params.debug.dump_ast) {
            CompilePhaseV("Temp output - Resolved", [&]() {
                Dump_Rust(FMT(params.outfile << "_1_ast.rs").c_str(), crate);
            });
        }

        if (params.last_stage == ProgramParams::STAGE_RESOLVE) {
            return 0;
        }

        // --------------------------------------
        // HIR Section
        // --------------------------------------
        // Construct the HIR beside the AST in the compilation object pool.
        ::HIR::Crate* hir_crate = CompilePhase<::HIR::Crate*>("HIR Lower", [&]() {
            return LowerHIR_FromAST(pool, crate);
        });
        memory_dump("HIR Gen");
        if (params.debug.dump_hir) {
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIR_Dump(os, *hir_crate);
            });
        }
        memory_dump("HIR");

        CompilePhaseV("Lifetime Elision", [&]() {
            ConvertHIR_LifetimeElision(*hir_crate);
        });

        // Replace type aliases (`type`) into the actual type
        // - Does simple replacements
        // - Done before bind so type alises can be used in patterns?
        CompilePhaseV("Resolve Type Aliases", [&]() {
            ConvertHIR_ExpandAliases(*hir_crate);
        });
        // Set up bindings and other useful information.
        CompilePhaseV("Resolve Bind", [&]() {
            ConvertHIR_Bind(*hir_crate);
        });

        // Determine what trait to use for <T>::Foo in outer scope
        // - Also inserts defaults in trait impls
        CompilePhaseV("Resolve UFCS Outer", [&]() {
            ConvertHIR_ResolveUFCS_Outer(*hir_crate);
        });
        // Expand `Self` into the true type
        // - TODO: Move this later on, but that requires fixing some of the resolve logic around trait impl lookup
        CompilePhaseV("Resolve HIR Self Type", [&]() {
            ConvertHIR_ExpandAliases_Self(*hir_crate);
        });
        // Enumerate marker impls on types and other useful metadata
        CompilePhaseV("Resolve HIR Markings", [&]() {
            ConvertHIR_Markings(*hir_crate);
        });
        CompilePhaseV("Sort Impls", [&]() {
            ConvertHIR_ResolveUFCS_SortImpls(*hir_crate);
        });
        // Determine what trait to use for <T>::Foo (and does some associated type expansion)
        CompilePhaseV("Resolve UFCS paths", [&]() {
            ConvertHIR_ResolveUFCS(*hir_crate);
        });
        if (params.debug.dump_hir) {
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIR_Dump(os, *hir_crate);
            });
        }
        // TODO: Expand vtables here?
        // - Some parts of constant evaluate require it
        // Basic constant evalulation (intergers/floats only)
        CompilePhaseV("Constant Evaluate", [&]() {
            ConvertHIR_ConstantEvaluate(*hir_crate);
        });

        if (params.debug.dump_hir) {
            // DUMP after initial consteval
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIR_Dump(os, *hir_crate);
            });
        }

        // === Type checking ===
        // - This can recurse and call the MIR lower to evaluate constants

        // Check outer items first (types of constants/functions/statics/impls/...)
        // - Doesn't do any expressions except those in types
        CompilePhaseV("Typecheck Outer", [&]() {
            Typecheck_ModuleLevel(*hir_crate);
        });
        // Check the rest of the expressions (including function bodies)
        CompilePhaseV("Typecheck Expressions", [&]() {
            Typecheck_Expressions(*hir_crate);
        });
        // === HIR Expansion ===
        // Annotate how each node's result is used
        CompilePhaseV("Expand HIR Annotate", [&]() {
            HIR_Expand_AnnotateUsage(*hir_crate);
        });
        CompilePhaseV("Expand HIR Static Borrow Mark", [&]() {
            HIR_Expand_StaticBorrowConstants_Mark(*hir_crate);
        });
        // - Needs to be done after static borrows, but before closures
        CompilePhaseV("Expand HIR Lifetimes", [&]() {
            HIR_Expand_LifetimeInfer(*hir_crate);
        });
        // - Now that all types are known, closures can be desugared
        CompilePhaseV("Expand HIR Closures", [&]() {
            HIR_Expand_Closures(*hir_crate);
        });
        CompilePhaseV("Expand HIR Static Borrow", [&]() {
            HIR_Expand_StaticBorrowConstants(*hir_crate);
        });
        // - Construct VTables for all traits and impls.
        //  TODO: How early can this be done?
        //  > Requires consteval completed for types to be fully valid?
        //  TODO: Would prefer to have this done before consteval, as consteval might reference a vtable
        CompilePhaseV("Expand HIR VTables", [&]() {
            HIR_Expand_VTables(*hir_crate);
        });
        // - And calls can be turned into UFCS
        CompilePhaseV("Expand HIR Calls", [&]() {
            HIR_Expand_UfcsEverything(*hir_crate);
        });
        CompilePhaseV("Expand HIR Reborrows", [&]() {
            HIR_Expand_Reborrows(*hir_crate);
        });
        CompilePhaseV("Expand HIR ErasedType", [&]() {
            HIR_Expand_ErasedType(*hir_crate);
        });
        if (params.debug.dump_hir) {
            // DUMP after typecheck (before validation)
            CompilePhaseV("Dump HIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_2_hir.rs"));
                HIR_Dump(os, *hir_crate);
            });
        }
        // - Ensure that typeck worked (including Fn trait call insertion etc)
        CompilePhaseV("Typecheck Expressions (validate)", [&]() {
            Typecheck_Expressions_Validate(*hir_crate);
        });
        // HACK?: Run lifetime inference again, so that bad closures are caught
        // - Doesn't quite work, can't seem to run this twice?
        //CompilePhaseV("Expand HIR Lifetimes (validate)", [&]() {
        //    HIR_Expand_LifetimeInfer_Validate(*hir_crate);
        //    });

        if (params.last_stage == ProgramParams::STAGE_TYPECK) {
            return 0;
        }
        memory_dump("Typecheck");

        // Lower expressions into MIR
        CompilePhaseV("Lower MIR", [&]() {
            HIR_GenerateMIR(*hir_crate);
        });

        if (params.debug.dump_mir) {
            // DUMP after generation
            CompilePhaseV("Dump MIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_3_mir.rs"));
                MIR_Dump(os, *hir_crate);
            });
        }
        memory_dump("MIR Gen");

        // LowerMIR validates every function before returning. The next validation is
        // performed after MIR_Cleanup has actually changed the crate.

        // - Expand constants in HIR and virtualise calls
        CompilePhaseV("MIR Cleanup", [&]() {
            MIR_CleanupCrate(*hir_crate);
        });
        if (params.debug.full_validate_early || getenv("MRUSTC_FULL_VALIDATE_PREOPT")) {
            CompilePhaseV("MIR Validate Full Early", [&]() {
                MIR_CheckCrate_Full(*hir_crate);
            });
        }

        // Optional for now
        if (params.run_borrowcheck) {
            CompilePhaseV("MIR Borrowcheck", [&]() {
                MIR_BorrowCheck_Crate(*hir_crate);
            });
        }

        // Optimise the MIR
        CompilePhaseV("MIR Optimise", [&]() {
            MIR_OptimiseCrate(*hir_crate, mir_opt_level, enable_mir_inlining);
        });

        if (params.debug.dump_mir) {
            // DUMP: After optimisation
            CompilePhaseV("Dump MIR", [&]() {
                ::std::ofstream os(FMT(params.outfile << "_3_mir.rs"));
                MIR_Dump(os, *hir_crate);
            });
        }
        CompilePhaseV("MIR Validate PO", [&]() {
            MIR_CheckCrate(*hir_crate);
        });
        // - Exhaustive MIR validation (follows every code path and checks variable validity)
        // > DEBUGGING ONLY
        CompilePhaseV("MIR Validate Full", [&]() {
            if (params.debug.full_validate || getenv("MRUSTC_FULL_VALIDATE")) {
                MIR_CheckCrate_Full(*hir_crate);
            }
        });

        if (params.last_stage == ProgramParams::STAGE_MIR) {
            return 0;
        }
        memory_dump("MIR Opt");

        // TODO: Pass to mark items that are..
        // - Signature Exportable (public)
        // - MIR Exportable (public generic, #[inline], or used by a either of those)
        // - Require codegen (public or used by an exported function)
        TransOptions trans_opt;
        trans_opt.mode = params.codegen.codegen_type == "" ? "c" : params.codegen.codegen_type;
        trans_opt.build_command_file = params.codegen.emit_build_command;
        trans_opt.opt_level = params.opt_level;
        trans_opt.panic_crate = "panic_" + params.codegen.panic_type;
        for (const char* libdir : params.lib_search_dirs) {
            // Store these paths for use in final linking.
            hir_crate->m_link_paths.push_back(libdir);
        }
        for (const char* libname : params.libraries) {
            hir_crate->m_ext_libs.push_back(::HIR::ExternLibrary{libname});
        }
        trans_opt.emit_debug_info = params.emit_debug_info;

        // Generate code for non-generic public items (if requested)
        if (params.test_harness) {
            // If the test harness is enabled, override crate type to "Executable"
            crate_type = ::AST::Crate::Type::Executable;
        }

        // TODO: For 1.29 executables/dylibs, add oom/panic shims
        if (crate_type == ::AST::Crate::Type::ProcMacro) {
            // - Save a very basic HIR dump, making sure that there's no lang items in it (e.g. `mrustc-main`)
            CompilePhaseV("HIR Serialise", [&]() {
                HIR::Crate crate_for_ser(pool, *types);
                crate_for_ser.m_crate_name = hir_crate->m_crate_name;
                crate_for_ser.m_edition = hir_crate->m_edition;
                for (const auto& i : hir_crate->m_root_module.m_macro_items) {
                    DEBUG(i.first << ": " << i.second->ent.tag_str());
                    if (const auto* e = i.second->ent.opt_ProcMacro()) {
                        crate_for_ser.m_root_module.m_macro_items.insert(std::make_pair(i.first, box$(HIR::VisEnt<HIR::MacroItem>{i.second->publicity, *e})));
                    }
                }
                crate_for_ser.m_exported_macro_names = hir_crate->m_exported_macro_names;
                HIR_Serialise(params.outfile + ".hir", crate_for_ser);
            });
        }

        // Enumerate items to be passed to codegen
        TransList items = CompilePhase<TransList>("Trans Enumerate", [&]() {
            switch (crate_type) {
                case ::AST::Crate::Type::Unknown:
                    ::std::cerr << "BUG? Unknown crate type" << ::std::endl;
                    exit(1);
                    break;
                case ::AST::Crate::Type::RustLib:
                case ::AST::Crate::Type::RustDylib:
                case ::AST::Crate::Type::CDylib:
                    return Trans_Enumerate_Public(*hir_crate);
                case ::AST::Crate::Type::ProcMacro:
                case ::AST::Crate::Type::Executable:
                    return Trans_Enumerate_Main(*hir_crate);
            }
            throw ::std::runtime_error("Invalid crate_type value");
        });
        // - Generate automatic impls (mainly Clone for 1.29)
        CompilePhaseV("Trans Auto Impls", [&]() {
            // TODO: Drop glue generation?
            Trans_AutoImpls(*hir_crate, items);
        });
        // - Generate monomorphised versions of all functions
        CompilePhaseV("Trans Monomorph", [&]() {
            Trans_Monomorphise_List(*hir_crate, items, mir_opt_level);
        });
        // - Do post-monomorph inlining
        CompilePhaseV("MIR Optimise Inline", [&]() {
            MIR_OptimiseCrate_Inlining(*hir_crate, items, false, mir_opt_level, enable_mir_inlining);
        });

        // - Expand constants in HIR (using ones that were monomorphised above)
        CompilePhaseV("MIR Cleanup 2", [&]() {
            MIR_Cleanup_SetPostMonomorph();
            MIR_CleanupCrate(*hir_crate);
        });

        memory_dump("Trans");

        std::string hir_file;
        switch (crate_type) {
            case ::AST::Crate::Type::RustLib:
                // Save a loadable HIR dump
                hir_file = params.outfile + ".hir";
                CompilePhaseV("HIR Serialise", [&]() {
                    HIR_Serialise(hir_file, *hir_crate);
                });
                break;
            case ::AST::Crate::Type::RustDylib:
                // Save a loadable HIR dump
                CompilePhaseV("HIR Serialise", [&]() {
                    //auto saved_ext_crates = ::std::move(hir_crate->m_ext_crates);
                    HIR_Serialise(hir_file, *hir_crate);
                    //hir_crate->m_ext_crates = ::std::move(saved_ext_crates);
                });
                break;
            default:
                break;
        }

        // - Do post-monomorph inlining
        CompilePhaseV("MIR Optimise Inline PostSave", [&]() {
            MIR_OptimiseCrate_Inlining(*hir_crate, items, true, mir_opt_level, enable_mir_inlining);
        });
        // - Clean up ununused functions
        CompilePhaseV("Trans Enumerate Cleanup", [&]() {
            Trans_Enumerate_Cleanup(*hir_crate, items);
        });

        switch (crate_type) {
            case ::AST::Crate::Type::Unknown:
                throw "";
            case ::AST::Crate::Type::RustLib:
                // Generate a linkable .o
                CompilePhaseV("Trans Codegen", [&]() {
                    Trans_Codegen(params.outfile, CodegenOutput::StaticLibrary, trans_opt, hir_crate, std::move(items), hir_file);
                });
                break;
            case ::AST::Crate::Type::RustDylib:
            case ::AST::Crate::Type::CDylib:
                // Generate a .so/.dll
                CompilePhaseV("Trans Codegen", [&]() {
                    Trans_Codegen(params.outfile, CodegenOutput::DynamicLibrary, trans_opt, hir_crate, std::move(items), hir_file);
                });
                break;
            case ::AST::Crate::Type::ProcMacro: {
                // Needs: An executable (the actual macro handler), metadata (for `extern crate foo;`)
                // - Metadata was done before enumerate
                CompilePhaseV("Trans Codegen", [&]() {
                    Trans_Codegen(params.outfile, CodegenOutput::Executable, trans_opt, hir_crate, std::move(items), hir_file);
                });
                break;
            }
            case ::AST::Crate::Type::Executable:
                CompilePhaseV("Trans Codegen", [&]() {
                    Trans_Codegen(params.outfile, CodegenOutput::Executable, trans_opt, hir_crate, std::move(items), "");
                });
                break;
        }
    } catch (unsigned int) {
    }
    //catch(const CompileError::Base& e)
    //{
    //    ::std::cerr << "Parser Error: " << e.what() << ::std::endl;
    //    return 2;
    //}
    //catch(const ::std::exception& e)
    //{
    //    ::std::cerr << "Misc Error: " << e.what() << ::std::endl;
    //    return 2;
    //}
    //catch(const char* e)
    //{
    //    ::std::cerr << "Internal Compiler Error: " << e << ::std::endl;
    //    return 2;
    //}

    return 0;
}

ProgramParams::ProgramParams(int argc, char* argv[]) {
    if (const auto* a = getenv("MRUSTC_LIBDIR")) {
        this->lib_search_dirs.push_back(a);
    }

    // Hacky command-line parsing
    for (int i = 1; i < argc; i++) {
        const char* arg = argv[i];

        // The following imitates rustc's version output (which the crate `rustc_version` tries to parse)
        // - Very much a hack
        if (strcmp(arg, "-vV") == 0) {
            const char* rustc_target = RUSTC_TARGET_VERSION;

            ::std::cout << "rustc " << rustc_target << ".100 (mrustc " << Version_GetString() << ")" << ::std::endl;
            ::std::cout << "binary: rustc" << ::std::endl;
            ::std::cout << "commit-hash: " << gsVersion_GitHash << ::std::endl;
            ::std::cout << "commit-date: UNKNOWN" << ::std::endl;
            ::std::cout << "build-date: " << gsVersion_BuildTime << ::std::endl;
            ::std::cout << "host: UNKNOWN" << ::std::endl;
            ::std::cout << "release: " << rustc_target << ".100" << ::std::endl;

            exit(0);
        }

        if (arg[0] != '-' || arg[1] == '\0') {
            if (this->infile == "") {
                this->infile = arg;
            } else {
                ::std::cerr << "Unexpected free argument" << ::std::endl;
                exit(1);
            }
        } else if (arg[1] != '-') {
            arg++; // eat '-'

            switch (*arg) {
                case 'L':
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            ::std::cerr << "Option " << arg << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        this->lib_search_dirs.push_back(argv[++i]);
                    } else {
                        this->lib_search_dirs.push_back(arg + 1);
                    }
                    continue;
                case 'l':
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            ::std::cerr << "Option " << arg << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        this->libraries.push_back(argv[++i]);
                    } else {
                        this->libraries.push_back(arg + 1);
                    }
                    continue;
                case 'A':
                case 'W':
                case 'D':
                case 'F': {
                    const auto flag = *arg;
                    const char* lint_name;
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            ::std::cerr << "Option -" << flag << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        lint_name = argv[++i];
                    } else {
                        lint_name = arg + 1;
                    }
                    if (lint_name[0] == '\0') {
                        ::std::cerr << "Option -" << flag << " requires an argument" << ::std::endl;
                        exit(1);
                    }
                    const auto level = flag == 'A' ? CfgLintLevel::Allow
                        : flag == 'W' ? CfgLintLevel::Warn
                        : flag == 'D' ? CfgLintLevel::Deny
                        : CfgLintLevel::Forbid;
                    Cfg_SetLintLevel(lint_name, level);
                    continue;
                }
                case 'C': {
                    ::std::string optname;
                    ::std::string optval;
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            ::std::cerr << "Option " << arg << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        optname = argv[++i];
                    } else {
                        optname = arg + 1;
                    }
                    auto eq_pos = optname.find('=');
                    if (eq_pos != ::std::string::npos) {
                        optval = optname.substr(eq_pos + 1);
                        optname.resize(eq_pos);
                    }
                    auto get_optval = [&]() {
                        if (eq_pos == ::std::string::npos) {
                            ::std::cerr << "Flag -C " << optname << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                    };
                    //auto no_optval = [&]() {
                    //    if(eq_pos != ::std::string::npos) {
                    //        ::std::cerr << "Flag -C " << optname << " doesn't take an argument" << ::std::endl;
                    //        exit(1);
                    //    }
                    //    };

                    if (optname == "emit-build-command") {
                        get_optval();
                        this->codegen.emit_build_command = optval;
                    } else if (optname == "codegen-type") {
                        get_optval();
                        this->codegen.codegen_type = optval;
                    } else if (optname == "emit-depfile") {
                        get_optval();
                        this->emit_depfile = optval;
                    } else if (optname == "panic") {
                        get_optval();
                        this->codegen.panic_type = optval;
                    } else {
                        ::std::cerr << "Unknown codegen option: '" << optname << "'" << ::std::endl;
                        exit(1);
                    }
                }
                    continue;
                case 'Z': {
                    ::std::string optname;
                    ::std::string optval;
                    if (arg[1] == '\0') {
                        if (i == argc - 1) {
                            ::std::cerr << "Option " << arg << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        optname = argv[++i];
                    } else {
                        optname = arg + 1;
                    }
                    auto eq_pos = optname.find('=');
                    if (eq_pos != ::std::string::npos) {
                        optval = optname.substr(eq_pos + 1);
                        optname.resize(eq_pos);
                    }
                    auto get_optval = [&]() {
                        if (eq_pos == ::std::string::npos) {
                            ::std::cerr << "Flag -Z " << optname << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                    };
                    auto no_optval = [&]() {
                        if (eq_pos != ::std::string::npos) {
                            ::std::cerr << "Flag -Z " << optname << " doesn't take an argument" << ::std::endl;
                            exit(1);
                        }
                    };

                    if (optname == "disable-mir-opt") {
                        no_optval();
                        this->mir_opt_level = 0;
                        this->mir_opt_level_explicit = true;
                    } else if (optname == "mir-opt-level") {
                        get_optval();
                        if (optval.empty()) {
                            ::std::cerr << "Invalid number for -Z mir-opt-level: '" << optval << "'" << ::std::endl;
                            exit(1);
                        }
                        unsigned value = 0;
                        for (const char c : optval) {
                            if (c < '0' || c > '9') {
                                ::std::cerr << "Invalid number for -Z mir-opt-level: '" << optval << "'" << ::std::endl;
                                exit(1);
                            }
                            const unsigned digit = c - '0';
                            if (value > (UINT_MAX - digit) / 10) {
                                ::std::cerr << "Number for -Z mir-opt-level is too large: '" << optval << "'" << ::std::endl;
                                exit(1);
                            }
                            value = value * 10 + digit;
                        }
                        this->mir_opt_level = value;
                        this->mir_opt_level_explicit = true;
                    } else if (optname == "next-solver") {
                        if (eq_pos == ::std::string::npos || optval == "globally") {
                            this->trait_solver.coherence = true;
                            this->trait_solver.globally = true;
                        } else if (optval == "coherence") {
                            this->trait_solver.coherence = true;
                            this->trait_solver.globally = false;
                        } else if (optval == "no") {
                            this->trait_solver.coherence = false;
                            this->trait_solver.globally = false;
                        } else {
                            ::std::cerr << "Invalid value for -Z next-solver: '" << optval
                                       << "' (expected 'no', 'coherence', or 'globally')"
                                       << ::std::endl;
                            exit(1);
                        }
                    } else if (optname == "full-validate") {
                        no_optval();
                        this->debug.full_validate = true;
                    } else if (optname == "full-validate-early") {
                        no_optval();
                        this->debug.full_validate_early = true;
                    } else if (optname == "dump-ast") {
                        no_optval();
                        this->debug.dump_ast = true;
                    } else if (optname == "dump-hir") {
                        no_optval();
                        this->debug.dump_hir = true;
                    } else if (optname == "dump-mir") {
                        no_optval();
                        this->debug.dump_mir = true;
                    } else if (optname == "stop-after") {
                        get_optval();
                        if (optval == "parse") {
                            this->last_stage = STAGE_PARSE;
                        } else if (optval == "expand") {
                            this->last_stage = STAGE_EXPAND;
                        } else if (optval == "resolve") {
                            this->last_stage = STAGE_RESOLVE;
                        } else if (optval == "typeck") {
                            this->last_stage = STAGE_TYPECK;
                        } else if (optval == "mir") {
                            this->last_stage = STAGE_MIR;
                        } else {
                            ::std::cerr << "Unknown argument to -Z stop-after - '" << optval << "'" << ::std::endl;
                            exit(1);
                        }
                    } else if (optname == "pause-after-start") {
                        this->debug.pause = true;
                    } else if (optname == "print-cfgs") {
                        no_optval();
                        this->print_cfgs = true;
                    } else if (optname == "check-cfg-all-expected") {
                        // This only controls how many expected cfg values rustc
                        // prints in diagnostics.  mrustc emits a compact
                        // diagnostic and has no corresponding display limit.
                        no_optval();
                    } else if (optname == "borrowcheck") {
                        no_optval();
                        this->run_borrowcheck = true;
                    } else {
                        ::std::cerr << "Unknown -Z flag: '" << optname << "'" << ::std::endl;
                        exit(1);
                    }
                }
                    continue;

                default:
                    // Fall through to the for loop below
                    break;
            }

            for (; *arg; arg++) {
                switch (*arg) {
                    // "-o <file>" : Set output file
                    case 'o':
                        if (i == argc - 1) {
                            ::std::cerr << "Option -" << *arg << " requires an argument" << ::std::endl;
                            exit(1);
                        }
                        this->outfile = argv[++i];
                        break;
                    case 'O':
                        this->opt_level = 2;
                        break;
                    case 'g':
                        this->emit_debug_info = true;
                        break;
                    default:
                        ::std::cerr << "Unknown option: '-" << *arg << "'" << ::std::endl;
                        exit(1);
                }
            }
        } else {
            auto check_with_arg = [&](const char* name) -> const char* {
                if (strcmp(arg + 2, name) == 0) {
                    if (i == argc - 1) {
                        ::std::cerr << "Flag " << arg << " requires an argument" << ::std::endl;
                        exit(1);
                    }
                    return argv[++i];
                }
                if (strncmp(arg + 2, name, strlen(name)) == 0 && arg[2 + strlen(name)] == '=') {
                    return arg + 2 + strlen(name) + 1;
                }
                return nullptr;
            };

            if (strcmp(arg, "--help") == 0) {
                this->show_help();
                exit(0);
            } else if (strcmp(arg, "--version") == 0) {
                const char* rustc_target = RUSTC_TARGET_VERSION;
                // NOTE: Starts the version with "rustc 1.29.100" so build scripts don't get confused
                ::std::cout << "rustc " << rustc_target << ".100 (mrustc " << Version_GetString() << ")" << ::std::endl;
                ::std::cout << "release: " << rustc_target << ".100" << ::std::endl; // `autoconfig` looks for this line
                ::std::cout << "- Build time: " << gsVersion_BuildTime << ::std::endl;
                ::std::cout << "- Commit: " << gsVersion_GitHash << (gbVersion_GitDirty ? " (dirty tree)" : "") << ::std::endl;
                exit(0);
            }
            // --out-dir <dir>  >> Set the output directory for automatically-named files
            else if (const char* out_dir = check_with_arg("out-dir")) {
                this->output_dir = out_dir;
                if (this->output_dir != "" && this->output_dir.back() != '/') {
                    this->output_dir += '/';
                }
            }
            // --extern <name>=<path>   >> Override the file to load for `extern crate <name>;`
            else if (strcmp(arg, "--extern") == 0) {
                if (i == argc - 1) {
                    ::std::cerr << "Option " << arg << " requires an argument" << ::std::endl;
                    exit(1);
                }
                const char* desc = argv[++i];
                const char* pos = ::std::strchr(desc, '=');
                if (pos == nullptr) {
                    ::std::cerr << "--extern takes an argument of the format name=path" << ::std::endl;
                    exit(1);
                }

                auto name = ::std::string(desc, pos);
                auto path = ::std::string(pos + 1);
                this->crate_overrides.insert(::std::make_pair(mv$(name), mv$(path)));
            }
            // --crate-tag <name>  >> Specify a version/identifier suffix for the crate
            else if (const auto* name_str = check_with_arg("crate-tag")) {
                this->crate_name_suffix = name_str;
            }
            // --crate-name <name>  >> Specify the crate name (overrides `#![crate_name="<name>"]`)
            else if (const auto* name_str = check_with_arg("crate-name")) {
                this->crate_name = name_str;
            }
            // `--crate-type <name>`    - Specify the crate type (overrides `#![crate_type="<name>"]`)
            else if (const char* type_str = check_with_arg("crate-type")) {
                if (strcmp(type_str, "lib") == 0 || strcmp(type_str, "rlib") == 0) {
                    this->crate_type = ::AST::Crate::Type::RustLib;
                } else if (strcmp(type_str, "dylib") == 0) {
                    this->crate_type = ::AST::Crate::Type::RustDylib;
                } else if (strcmp(type_str, "bin") == 0) {
                    this->crate_type = ::AST::Crate::Type::Executable;
                } else if (strcmp(type_str, "proc-macro") == 0) {
                    this->crate_type = ::AST::Crate::Type::ProcMacro;
                } else {
                    ::std::cerr << "Unknown value for --crate-type: " << type_str << ::std::endl;
                    exit(1);
                }
            }
            // `--cfg <flag>` / `--cfg=<flag>`
            // `--cfg <var>=<value>` / `--cfg=<var>=<value>`
            else if (const char* cfg_spec = check_with_arg("cfg")) {
                ::std::string name;
                ::std::string value;
                ::std::string error;
                bool has_value = false;
                if (!Cfg_ParseOption(cfg_spec, name, has_value, value, error)) {
                    ::std::cerr << "invalid `--cfg` argument: `" << cfg_spec << "`: " << error << ::std::endl;
                    exit(1);
                }
                if (has_value) {
                    if (name == "feature") {
                        this->features.insert(value);
                    } else {
                        Cfg_SetValue(mv$(name), mv$(value));
                    }
                } else {
                    Cfg_SetFlag(mv$(name));
                }
            } else if (const char* check_cfg_spec = check_with_arg("check-cfg")) {
                ::std::string error;
                if (!Cfg_SetCheckSpec(check_cfg_spec, error)) {
                    ::std::cerr << "invalid `--check-cfg` argument: `" << check_cfg_spec << "`: " << error << ::std::endl;
                    exit(1);
                }
            } else if (const char* force_warn = check_with_arg("force-warn")) {
                if (force_warn[0] == '\0') {
                    ::std::cerr << "Flag --force-warn requires an argument" << ::std::endl;
                    exit(1);
                }
                Cfg_SetLintLevel(force_warn, CfgLintLevel::ForceWarn);
            } else if (const char* lint_cap = check_with_arg("cap-lints")) {
                CfgLintLevel level;
                if (strcmp(lint_cap, "allow") == 0) {
                    level = CfgLintLevel::Allow;
                } else if (strcmp(lint_cap, "warn") == 0) {
                    level = CfgLintLevel::Warn;
                } else if (strcmp(lint_cap, "deny") == 0) {
                    level = CfgLintLevel::Deny;
                } else if (strcmp(lint_cap, "forbid") == 0) {
                    level = CfgLintLevel::Forbid;
                } else {
                    ::std::cerr << "unknown lint level: `" << lint_cap << "`" << ::std::endl;
                    exit(1);
                }
                Cfg_SetLintCap(level);
            } else if (const char* emit = check_with_arg("emit")) {
                ::std::cerr << "Ignoring `--emit " << emit << "` for compatability with rustc" << std::endl;
            }
            // `--target <triple>`  - Override the default compiler target
            else if (const char* target_name = check_with_arg("target")) {
                this->target = target_name;
            } else if (strcmp(arg, "--dump-target-spec") == 0) {
                if (i == argc - 1) {
                    ::std::cerr << "Flag " << arg << " requires an argument" << ::std::endl;
                    exit(1);
                }
                this->target_saveback = argv[++i];
            } else if (strcmp(arg, "--test") == 0) {
                this->test_harness = true;
            } else if (const char* edition_str = check_with_arg("edition")) {
                if (strcmp(edition_str, "2015") == 0) {
                    this->edition = AST::Edition::Rust2015;
                } else if (strcmp(edition_str, "2018") == 0) {
                    this->edition = AST::Edition::Rust2018;
                } else if (strcmp(edition_str, "2021") == 0) {
                    this->edition = AST::Edition::Rust2021;
                } else if (strcmp(edition_str, "2024") == 0) {
                    this->edition = AST::Edition::Rust2024;
                } else {
                    ::std::cerr << "Unknown value for " << arg << " - '" << edition_str << "'" << ::std::endl;
                    exit(1);
                }
            } else {
                ::std::cerr << "Unknown option '" << arg << "'" << ::std::endl;
                exit(1);
            }
        }
    }

    if (const auto* a = getenv("MRUSTC_DUMP")) {
        while (a[0]) {
            const char* end = strchr(a, ':');

            ::stdx::string_view s;
            if (end) {
                s = ::stdx::string_view{a, end};
                a = end + 1;
            } else {
                end = a + strlen(a);
                s = ::stdx::string_view{a, end};
                a = end;
            }

            if (s == "") {
                // Ignore
            } else if (s == "ast") {
                this->debug.dump_ast = true;
            } else if (s == "hir") {
                this->debug.dump_hir = true;
            } else if (s == "mir") {
                this->debug.dump_mir = true;
            } else {
                ::std::cerr << "Unknown option in $MRUSTC_DUMP '" << s << "'" << ::std::endl;
                // - No terminate, just warn
            }
        }
    }
}

void ProgramParams::show_help() const {
    ::std::cout << "USAGE: mrustc <sourcefile>\n"
                   "\n"
                   "OPTIONS:\n"
                   "-L <dir>           : Search for crate files (.hir) in this directory\n"
                   "-o <filename>      : Write compiler output (library or executable) to this file\n"
                   "-O                 : Enable optimisation\n"
                   "-g                 : Emit debugging information\n"
                   "--out-dir <dir>    : Specify the output directory (alternative to `-o`)\n"
                   "--extern <crate>=<path>\n"
                   "                   : Specify the path for a given crate (instead of searching for it)\n"
                   "--crate-tag <str>  : Specify a suffix for symbols and output files\n"
                   "--crate-name <str> : Override/set the crate name\n"
                   "--crate-type <ty>  : Override/set the crate type (rlib, bin, proc-macro)\n"
                   "--cfg flag         : Set a boolean #[cfg]/cfg! flag\n"
                   "--cfg flag=\"val\"   : Set a string #[cfg]/cfg! flag\n"
                   "--target <name>    : Compile code for the given target\n"
                   "--test             : Generate a unit test executable\n"
                   "-C <option>        : Code-generation options\n"
                   "-Z <option>        : Debugging/experimental options\n";
}
