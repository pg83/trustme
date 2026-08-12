#include "expand_proc_macro.h"
#include "synext.h"
#include "common.h"
#include "expand_cfg.h"
#include "ast_ast.h"
#include "ast_expr.h"
#include "ast_crate.h"
#include "main_bindings.h"
#include "ast_dump.h"
#include "hir_hir.h" // ABI_RUST
#include "parse_lex.h"
#include "parse_ttstream.h"
#include <unordered_set>
#include <unistd.h> // read/write/pipe
#include <spawn.h>
#include <sys/wait.h>

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__APPLE__)
extern char** environ;
#endif

#define NEWNODE(ty, ...) ::AST::ExprNodeP(new ::AST::ExprNode##ty(__VA_ARGS__))

class DecoratorProcMacroDerive: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const AST::Attribute& attr, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (i.is_None()) {
            return;
        }

        if (!i.is_Function()) {
            TODO(sp, "Error for proc_macro_derive on non-Function");
        }

        ::std::vector<::std::string> attributes;
        TTStream lex(sp, ParseState(), attr.data());
        lex.getTokenCheck(TOK_PAREN_OPEN);
        auto trait_name = lex.getTokenCheck(TOK_IDENT).ident().name;
        while (lex.getTokenIf(TOK_COMMA)) {
            if (lex.lookahead(0) == TOK_PAREN_CLOSE) {
                break;
            }
            auto k = lex.getTokenCheck(TOK_IDENT).ident().name;
            if (k == "attributes") {
                lex.getTokenCheck(TOK_PAREN_OPEN);
                do {
                    if (lex.lookahead(0) == TOK_PAREN_CLOSE) {
                        break;
                    }
                    attributes.push_back(lex.getTokenCheck(TOK_IDENT).ident().name.c_str());
                } while (lex.getTokenIf(TOK_COMMA));
                lex.getTokenCheck(TOK_PAREN_CLOSE);
            } else {
                ERROR(sp, E0000, "Unexpected `" << k << "` in `#[proc_macro_derive]`");
            }
        }
        lex.getTokenCheck(TOK_PAREN_CLOSE);

        crate.procMacros.push_back(AST::ProcMacroDef{AST::ProcMacroTy::Derive, RcString::newInterned(FMT(trait_name)), path, mv$(attributes)});
    }
};
STATIC_DECORATOR("proc_macro_derive", DecoratorProcMacroDerive)

class DecoratorProcMacroAttribute: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const AST::Attribute& attr, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (i.is_None()) {
            return;
        }

        if (!i.is_Function()) {
            TODO(sp, "Error for #[proc_macro_attribute] on non-Function");
        }

        crate.procMacros.push_back(AST::ProcMacroDef{AST::ProcMacroTy::Attribute, path.nodes.back(), path, {}});
    }
};
STATIC_DECORATOR("proc_macro_attribute", DecoratorProcMacroAttribute)

class DecoratorProcMacro: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const AST::Attribute& attr, ::AST::Crate& crate, const AST::AbsolutePath& path, AST::Module&, size_t, slice<const AST::Attribute> attrs, const AST::Visibility& vis, AST::Item& i) const override {
        if (i.is_None()) {
            return;
        }

        if (!i.is_Function()) {
            TODO(sp, "Error for #[proc_macro] on non-Function");
        }

        crate.procMacros.push_back(AST::ProcMacroDef{AST::ProcMacroTy::Function, path.nodes.back(), path, {}});
    }
};
STATIC_DECORATOR("proc_macro", DecoratorProcMacro)

void ExpandProcMacroHarness(::AST::Crate& crate) {
    auto pm_crate_name = RcString::newInterned("proc_macro");
    AST::gImplicitCrates.insert(std::make_pair(pm_crate_name, crate.loadExternCrate(Span(), pm_crate_name)));

    // Create the following module:
    // ```
    // mod `proc_macro#` {
    //   extern crate proc_macro;
    //   fn main() {
    //     self::proc_macro::main(&::`proc_macro#`::MACROS);
    //   }
    //   static TESTS: [proc_macro::MacroDesc; _] = [
    //     proc_macro::MacroDesc { name: "deriving_Foo", handler: ::path::to::foo }
    //     ];
    // }
    // ```

    // ---- main function ----
    auto mainFn = ::AST::Function{Span(), TypeRef(TypeRef::TagUnit(), Span()), {}};
    {
        auto callNode = NEWNODE(CallPath, ::AST::Path(crate.extCratenameProcmacro, {::AST::PathNode("main")}), ::makeVec1(NEWNODE(UniOp, ::AST::ExprNodeUniOp::REF, NEWNODE(NamedValue, ::AST::Path("", {::AST::PathNode("proc_macro#"), ::AST::PathNode("MACROS")})))));
        mainFn.set_code(mv$(callNode));
    }

    // ---- test list ----
    ::std::vector<::AST::ExprNodeP> test_nodes;

    for (const auto& desc : crate.procMacros) {
        const char* type_name = "SingleStream";
        switch (desc.ty) {
            case ::AST::ProcMacroTy::Attribute:
                type_name = "Attribute";
                break;
            default:
                break;
        }
        ::AST::ExprNodeStructLiteral::t_values descVals;
        // `name: "foo",`
        descVals.push_back({{}, "name", NEWNODE(String, desc.name.c_str())});
        // `handler`: ::foo
        descVals.push_back({{}, "handler", NEWNODE(CallPath, ::AST::Path(crate.extCratenameProcmacro, {::AST::PathNode("MacroType"), ::AST::PathNode(type_name)}), ::makeVec1(NEWNODE(NamedValue, AST::Path(desc.path))))});

        test_nodes.push_back(NEWNODE(StructLiteral, ::AST::Path(crate.extCratenameProcmacro, {::AST::PathNode("MacroDesc")}), nullptr, mv$(descVals)));
    }
    auto* tests_array = new ::AST::ExprNodeArray(mv$(test_nodes));

    size_t test_count = tests_array->values.size();
    auto tests_list = ::AST::Static{::AST::Static::Class::STATIC, TypeRef(TypeRef::TagSizedArray(), Span(), TypeRef(Span(), ::AST::Path(crate.extCratenameProcmacro, {::AST::PathNode("MacroDesc")})), ::std::shared_ptr<::AST::ExprNode>(new ::AST::ExprNodeInteger(U128(test_count), CORETYPE_UINT))), ::AST::Expr(mv$(tests_array))};

    // ---- module ----
    auto newmod = ::AST::Module{::AST::AbsolutePath("", {"proc_macro#"})};
    // - TODO: These need to be loaded too.
    //  > They don't actually need to exist here, just be loaded (and use absolute paths)
    auto vis_private = AST::Visibility::makeRestricted(AST::Visibility::Ty::Private, newmod.path());
    newmod.addExtCrate(Span(), vis_private, crate.extCratenameProcmacro, "proc_macro", {});

    newmod.addItem(Span(), vis_private, "main", mv$(mainFn), {});
    newmod.addItem(Span(), vis_private, "MACROS", mv$(tests_list), {});

    crate.rootModule.addItem(Span(), vis_private, "proc_macro#", mv$(newmod), {});
    crate.mLangItems["mrustc-main"] = ::AST::AbsolutePath("", {"proc_macro#", "main"});
}

enum class TokenClass {
    EndOfStream = 0,
    Symbol = 1,
    Ident = 2,
    Lifetime = 3,
    String = 4,
    ByteString = 5, // String
    CharLit = 6,    // v128
    UnsignedInt = 7,
    SignedInt = 8,
    Float = 9,
    SpanRef = 10,
    SpanDef = 11,
};
enum class FragType {
    Ident = 0,
    Tt = 1,

    Path = 2,
    Type = 3,

    Expr = 4,
    Statement = 5,
    Block = 6,
    Pattern = 7,
};

struct ProcMacroInv: public TokenStream {
    Span parentSpan;
    Span thisSpan;
    const ::HIR::ProcMacro& procMacroDesc;
    AST::Edition edition;
    ::std::ofstream dumpFileOut;
    ::std::ofstream dumpFileRes;

    /// Spans that have had an index assigned
    ::std::unordered_map<const SpanInner*, size_t> knownSpans;
    /// Span indexes that have been sent
    ::std::unordered_set<size_t> sent_spans;
    size_t nextSpanIndex = 2;

    struct Handles {
        //~Handles();
        Handles() {
        }

        Handles(Handles&&);
        Handles(const Handles&) = delete;
        Handles& operator=(Handles&&) = delete;
        Handles& operator=(const Handles&) = delete;
        pid_t childPid = 0; // Questionably needed
        int childStdin = -1;
        int childStdout = -1;
        // NOTE: stderr stays as our stderr
    } handles;

    bool eofHit = false;

public:
    ProcMacroInv(const Span& sp, AST::Edition edition, const char* executable, const ::HIR::ProcMacro& proc_macro_desc);
    ProcMacroInv(const ProcMacroInv&) = delete;
    ProcMacroInv(ProcMacroInv&&) = default;
    ProcMacroInv& operator=(const ProcMacroInv&) = delete;
    ProcMacroInv& operator=(ProcMacroInv&&) = delete;
    ~ProcMacroInv();

    bool checkGood();

    void send_done() {
        this->send_u8(static_cast<uint8_t>(TokenClass::EndOfStream));
        dumpFileOut.flush();
        DEBUG("Input tokens sent");
    }

    void send_symbol(const char* val) {
        this->send_u8(static_cast<uint8_t>(TokenClass::Symbol));
        this->send_bytes(val, ::std::strlen(val));
    }

    void send_rword(const char* val) {
        this->send_u8(static_cast<uint8_t>(TokenClass::Ident));
        this->send_bytes(val, ::std::strlen(val));
    }

    void send_ident(const char* val) {
        this->send_u8(static_cast<uint8_t>(TokenClass::Ident));
        if (LexFindReservedWord(val, edition) != TOK_NULL) {
            auto size = ::std::strlen(val);
            this->send_v128u(2 + size);
            this->send_bytes_raw("r#", 2);
            this->send_bytes_raw(val, size);
        } else {
            this->send_bytes(val, ::std::strlen(val));
        }
    }

    void send_ident(const Ident& val) {
        send_ident(val.name.c_str());
    }

    void send_lifetime(const char* val) {
        this->send_u8(static_cast<uint8_t>(TokenClass::Lifetime));
        this->send_bytes(val, ::std::strlen(val));
    }

    void send_string(const ::std::string& s) {
        this->send_u8(static_cast<uint8_t>(TokenClass::String));
        this->send_bytes(s.data(), s.size());
    }

    void send_bytestring(const ::std::string& s) {
        this->send_u8(static_cast<uint8_t>(TokenClass::ByteString));
        this->send_bytes(s.data(), s.size());
    }

    void send_char(uint32_t ch) {
        this->send_u8(static_cast<uint8_t>(TokenClass::CharLit));
        this->send_v128u(ch);
    }

    void send_int(eCoreType ct, U128 v) {
        uint8_t size;
        switch (ct) {
            case CORETYPE_ANY:
                size = 0;
                if (0) {
                    case CORETYPE_UINT:
                        size = 1;
                }
                if (0) {
                    case CORETYPE_U8:
                        size = 8;
                }
                if (0) {
                    case CORETYPE_U16:
                        size = 16;
                }
                if (0) {
                    case CORETYPE_U32:
                        size = 32;
                }
                if (0) {
                    case CORETYPE_U64:
                        size = 64;
                }
                if (0) {
                    case CORETYPE_U128:
                        size = 128;
                }
                if (0)
                    ;
                this->send_u8(static_cast<uint8_t>(TokenClass::UnsignedInt));
                this->send_u8(size);
                break;
            case CORETYPE_INT:
                size = 1;
                if (0) {
                    case CORETYPE_I8:
                        size = 8;
                }
                if (0) {
                    case CORETYPE_I16:
                        size = 16;
                }
                if (0) {
                    case CORETYPE_I32:
                        size = 32;
                }
                if (0) {
                    case CORETYPE_I64:
                        size = 64;
                }
                if (0) {
                    case CORETYPE_I128:
                        size = 128;
                }
                if (0)
                    ;
                this->send_u8(static_cast<uint8_t>(TokenClass::SignedInt));
                this->send_u8(size);
                break;
            default:
                BUG(parentSpan, "Unknown integer type");
        }
        this->send_v128u(v);
    }

    void send_float(eCoreType ct, FloatValue v) {
        this->send_u8(static_cast<uint8_t>(TokenClass::Float));
        switch (ct) {
            case CORETYPE_ANY:
                this->send_u8(0);
                break;
            case CORETYPE_F32:
                this->send_u8(32);
                break;
            case CORETYPE_F64:
                this->send_u8(64);
                break;
            default:
                BUG(parentSpan, "Unknown float type");
        }
        double wire_value = static_cast<double>(v);
        this->send_bytes_raw(&wire_value, sizeof(wire_value));
    }

    void send_span_def(size_t index, const Span& sp) {
        this->knownSpans[sp.get()] = index;
        this->sent_spans.insert(index);

        this->send_u8(static_cast<uint8_t>(TokenClass::SpanDef));
        this->send_v128u(index);
        this->send_v128u(0); // TODO: Parent span
        if (const auto* sp_p = cast<const SpanInnerSource>(sp.get())) {
            this->send_bytes(sp_p->filename.c_str(), sp_p->filename.size());
            this->send_u8(1); // path_is_real
            this->send_v128u(sp_p->start_line);
            this->send_v128u(sp_p->endLine);
            this->send_v128u(sp_p->start_ofs);
            this->send_v128u(sp_p->endOfs);
        } else {
            this->send_bytes("MACRO", 5); // TODO: better filename?
            this->send_u8(0);             // path_is_real
            this->send_v128u(0);
            this->send_v128u(0);
            this->send_v128u(0);
            this->send_v128u(0);
        }
    }

    bool attrIsUsed(const RcString& n) const {
        if (n == "repr") {
            return true;
        }
        return ::std::find(procMacroDesc.attributes.begin(), procMacroDesc.attributes.end(), n) != procMacroDesc.attributes.end();
    }

    virtual Position getPosition() const override;
    virtual Token realGetToken() override;

    virtual AST::Edition realGetEdition() const override {
        return edition;
    }

    virtual Ident::Hygiene realGetHygiene() const override;

private:
    Token realGetToken_();
    void send_u8(uint8_t v);
    void send_bytes(const void* val, size_t size);
    void send_bytes_raw(const void* val, size_t size);
    void send_v128u(uint64_t val);
    void send_v128u(U128 val);

    uint8_t recv_u8();
    ::std::string recv_bytes();
    void recv_bytes_raw(void* out_void, size_t len);
    uint64_t recv_v128u();
    U128 recv_v128u_u128();
};

ProcMacroInv ProcMacroInvokeInt(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& macPath) {
    TRACE_FUNCTION_F(macPath);
    // 1. Locate macro in HIR list
    const auto& crate_name = macPath.front();
    ASSERT_BUG(sp, crate.externCrates.count(crate_name), "Crate not loaded for macro: [" << macPath << "]");
    const auto& ext_crate = crate.externCrates.at(crate_name);
    // TODO: Ensure that this macro is in the listed crate.
    const ::HIR::ProcMacro* pmp = nullptr;
    for (const auto& mi : ext_crate.hir->rootModule.macroItems) {
        if (!mi.second->ent.is_ProcMacro()) {
            continue;
        }
        const auto& pm = mi.second->ent.as_ProcMacro();
        bool good = true;
        for (size_t i = 0; i < ::std::min(macPath.size() - 1, pm.path.components().size()); i++) {
            if (macPath[1 + i] != pm.path.components()[i]) {
                good = false;
                break;
            }
        }
        if (good) {
            pmp = &pm;
            break;
        }
    }
    if (!pmp) {
        ERROR(sp, E0000, "Unable to find referenced proc macro " << macPath);
    }

    // 2. Get executable and macro name
    ::std::string proc_macro_exe_name = ext_crate.filename;

    // 3. Create ProcMacroInv
    auto rv = ProcMacroInv(sp, ext_crate.hir->edition, proc_macro_exe_name.c_str(), *pmp);
    rv.parse_state().crate = &crate;

    return rv;

    // NOTE: 1.39 failure_derive (2015) emits `::failure::foo` but `libcargo` doesn't have `failure` in root (it's a 2018 crate)
    //return ProcMacroInv(sp, ext_crate.m_hir->m_edition, proc_macro_exe_name.c_str(), *pmp);
}

namespace {
    struct Visitor {
        const Span& sp;
        ProcMacroInv& pmi;
        bool emitAllAttrs;
        // Derive inputs must not include `#[derive(...)]` attributes themselves (rustc
        // strips them before invoking a derive macro).
        bool skip_derive_attrs = false;

        Visitor(const Span& sp, ProcMacroInv& pmi)
            : sp(sp)
            , pmi(pmi)
            //,emit_all_attrs(false)
            , emitAllAttrs(true)
        {
        }

        void visit_bound_constness(AST::BoundConstness constness) {
            if (constness == AST::BoundConstness::Always) {
                pmi.send_rword("const");
            } else if (constness == AST::BoundConstness::Maybe) {
                pmi.send_symbol("[");
                pmi.send_rword("const");
                pmi.send_symbol("]");
            }
        }

        void visit_token(const ::Token& tok) {
            switch (tok.type()) {
                case TOK_NULL:
                    BUG(sp, "Unexpected NUL in token stream");
                case TOK_EOF:
                    BUG(sp, "Unexpected EOF in token stream");

                case TOK_NEWLINE:
                case TOK_WHITESPACE:
                case TOK_COMMENT:
                    BUG(sp, "Unexpected whitepace in tokenstream");
                    break;
                case TOK_INTERPOLATED_TYPE:
                    visit_type(const_cast<::Token&>(tok).fragType());
                    break;
                case TOK_INTERPOLATED_PATH:
                    TODO(sp, "TOK_INTERPOLATED_PATH");
                case TOK_INTERPOLATED_PATTERN:
                    TODO(sp, "TOK_INTERPOLATED_PATTERN");
                case TOK_INTERPOLATED_STMT:
                case TOK_INTERPOLATED_BLOCK:
                case TOK_INTERPOLATED_EXPR:
                    visit_node(const_cast<::Token&>(tok).fragNode());
                    break;
                case TOK_INTERPOLATED_META:
                case TOK_INTERPOLATED_STMT_ITEM:
                case TOK_INTERPOLATED_ITEM:
                case TOK_INTERPOLATED_VIS:
                    TODO(sp, "TOK_INTERPOLATED_...");
                // Value tokens
                case TOK_IDENT:
                    pmi.send_ident(tok.ident().name.c_str());
                    break; // TODO: Raw idents
                case TOK_LIFETIME:
                    pmi.send_lifetime(tok.ident().name.c_str());
                    break; // TODO: Hygine?
                case TOK_INTEGER:
                    if (tok.datatype() == CORETYPE_CHAR) {
                        pmi.send_char(tok.intval().truncate_u64());
                    } else {
                        pmi.send_int(tok.datatype(), tok.intval());
                    }
                    break;
                case TOK_CHAR:
                    pmi.send_char(tok.intval().truncate_u64());
                    break;
                case TOK_FLOAT:
                    pmi.send_float(tok.datatype(), tok.floatval());
                    break;
                case TOK_STRING:
                    pmi.send_string(tok.str());
                    break;
                case TOK_BYTESTRING:
                    pmi.send_bytestring(tok.str());
                    break;
                case TOK_CSTRING:
                    TODO(sp, "TOK_CSTRING");

                case TOK_HASH:
                    pmi.send_symbol("#");
                    break;
                case TOK_UNDERSCORE:
                    pmi.send_rword("_");
                    break;

                // Symbols
                case TOK_PAREN_OPEN:
                    pmi.send_symbol("(");
                    break;
                case TOK_PAREN_CLOSE:
                    pmi.send_symbol(")");
                    break;
                case TOK_BRACE_OPEN:
                    pmi.send_symbol("{");
                    break;
                case TOK_BRACE_CLOSE:
                    pmi.send_symbol("}");
                    break;
                case TOK_LT:
                    pmi.send_symbol("<");
                    break;
                case TOK_GT:
                    pmi.send_symbol(">");
                    break;
                case TOK_SQUARE_OPEN:
                    pmi.send_symbol("[");
                    break;
                case TOK_SQUARE_CLOSE:
                    pmi.send_symbol("]");
                    break;
                case TOK_COMMA:
                    pmi.send_symbol(",");
                    break;
                case TOK_SEMICOLON:
                    pmi.send_symbol(";");
                    break;
                case TOK_COLON:
                    pmi.send_symbol(":");
                    break;
                case TOK_DOUBLE_COLON:
                    pmi.send_symbol("::");
                    break;
                case TOK_STAR:
                    pmi.send_symbol("*");
                    break;
                case TOK_AMP:
                    pmi.send_symbol("&");
                    break;
                case TOK_PIPE:
                    pmi.send_symbol("|");
                    break;

                case TOK_FATARROW:
                    pmi.send_symbol("=>");
                    break;
                case TOK_THINARROW:
                    pmi.send_symbol("->");
                    break;
                case TOK_THINARROW_LEFT:
                    pmi.send_symbol("<-");
                    break;

                case TOK_PLUS:
                    pmi.send_symbol("+");
                    break;
                case TOK_DASH:
                    pmi.send_symbol("-");
                    break;
                case TOK_EXCLAM:
                    pmi.send_symbol("!");
                    break;
                case TOK_PERCENT:
                    pmi.send_symbol("%");
                    break;
                case TOK_SLASH:
                    pmi.send_symbol("/");
                    break;

                case TOK_DOT:
                    pmi.send_symbol(".");
                    break;
                case TOK_DOUBLE_DOT:
                    pmi.send_symbol("..");
                    break;
                case TOK_DOUBLE_DOT_EQUAL:
                    pmi.send_symbol("..=");
                    break;
                case TOK_TRIPLE_DOT:
                    pmi.send_symbol("...");
                    break;

                case TOK_EQUAL:
                    pmi.send_symbol("=");
                    break;
                case TOK_PLUS_EQUAL:
                    pmi.send_symbol("+=");
                    break;
                case TOK_DASH_EQUAL:
                    pmi.send_symbol("-");
                    break;
                case TOK_PERCENT_EQUAL:
                    pmi.send_symbol("%=");
                    break;
                case TOK_SLASH_EQUAL:
                    pmi.send_symbol("/=");
                    break;
                case TOK_STAR_EQUAL:
                    pmi.send_symbol("*=");
                    break;
                case TOK_AMP_EQUAL:
                    pmi.send_symbol("&=");
                    break;
                case TOK_PIPE_EQUAL:
                    pmi.send_symbol("|=");
                    break;

                case TOK_DOUBLE_EQUAL:
                    pmi.send_symbol("==");
                    break;
                case TOK_EXCLAM_EQUAL:
                    pmi.send_symbol("!=");
                    break;
                case TOK_GTE:
                    pmi.send_symbol(">=");
                    break;
                case TOK_LTE:
                    pmi.send_symbol("<=");
                    break;

                case TOK_DOUBLE_AMP:
                    pmi.send_symbol("&&");
                    break;
                case TOK_DOUBLE_PIPE:
                    pmi.send_symbol("||");
                    break;
                case TOK_DOUBLE_LT:
                    pmi.send_symbol("<<");
                    break;
                case TOK_DOUBLE_GT:
                    pmi.send_symbol(">>");
                    break;
                case TOK_DOUBLE_LT_EQUAL:
                    pmi.send_symbol("<=");
                    break;
                case TOK_DOUBLE_GT_EQUAL:
                    pmi.send_symbol(">=");
                    break;

                case TOK_DOLLAR:
                    pmi.send_symbol("$");
                    break;

                case TOK_QMARK:
                    pmi.send_symbol("?");
                    break;
                case TOK_AT:
                    pmi.send_symbol("@");
                    break;
                case TOK_TILDE:
                    pmi.send_symbol("~");
                    break;
                case TOK_BACKSLASH:
                    pmi.send_symbol("\\");
                    break;
                case TOK_CARET:
                    pmi.send_symbol("^");
                    break;
                case TOK_CARET_EQUAL:
                    pmi.send_symbol("^=");
                    break;
                case TOK_BACKTICK:
                    pmi.send_symbol("`");
                    break;

                // Reserved Words
                case TOK_RWORD_PUB:
                    pmi.send_rword("pub");
                    break;
                case TOK_RWORD_PRIV:
                    pmi.send_rword("priv");
                    break;
                case TOK_RWORD_MUT:
                    pmi.send_rword("mut");
                    break;
                case TOK_RWORD_CONST:
                    pmi.send_rword("const");
                    break;
                case TOK_RWORD_STATIC:
                    pmi.send_rword("static");
                    break;
                case TOK_RWORD_UNSAFE:
                    pmi.send_rword("unsafe");
                    break;
                case TOK_RWORD_EXTERN:
                    pmi.send_rword("extern");
                    break;
                case TOK_RWORD_CRATE:
                    pmi.send_rword("crate");
                    break;
                case TOK_RWORD_MOD:
                    pmi.send_rword("mod");
                    break;
                case TOK_RWORD_STRUCT:
                    pmi.send_rword("struct");
                    break;
                case TOK_RWORD_ENUM:
                    pmi.send_rword("enum");
                    break;
                case TOK_RWORD_TRAIT:
                    pmi.send_rword("trait");
                    break;
                case TOK_RWORD_FN:
                    pmi.send_rword("fn");
                    break;
                case TOK_RWORD_USE:
                    pmi.send_rword("use");
                    break;
                case TOK_RWORD_IMPL:
                    pmi.send_rword("impl");
                    break;
                case TOK_RWORD_TYPE:
                    pmi.send_rword("type");
                    break;
                case TOK_RWORD_WHERE:
                    pmi.send_rword("where");
                    break;
                case TOK_RWORD_AS:
                    pmi.send_rword("as");
                    break;
                case TOK_RWORD_LET:
                    pmi.send_rword("let");
                    break;
                case TOK_RWORD_MATCH:
                    pmi.send_rword("match");
                    break;
                case TOK_RWORD_IF:
                    pmi.send_rword("if");
                    break;
                case TOK_RWORD_ELSE:
                    pmi.send_rword("else");
                    break;
                case TOK_RWORD_LOOP:
                    pmi.send_rword("loop");
                    break;
                case TOK_RWORD_WHILE:
                    pmi.send_rword("while");
                    break;
                case TOK_RWORD_FOR:
                    pmi.send_rword("for");
                    break;
                case TOK_RWORD_IN:
                    pmi.send_rword("in");
                    break;
                case TOK_RWORD_DO:
                    pmi.send_rword("do");
                    break;
                case TOK_RWORD_CONTINUE:
                    pmi.send_rword("continue");
                    break;
                case TOK_RWORD_BREAK:
                    pmi.send_rword("break");
                    break;
                case TOK_RWORD_RETURN:
                    pmi.send_rword("return");
                    break;
                case TOK_RWORD_YIELD:
                    pmi.send_rword("yeild");
                    break;
                case TOK_RWORD_BOX:
                    pmi.send_rword("box");
                    break;
                case TOK_RWORD_REF:
                    pmi.send_rword("ref");
                    break;
                case TOK_RWORD_FALSE:
                    pmi.send_rword("false");
                    break;
                case TOK_RWORD_TRUE:
                    pmi.send_rword("true");
                    break;
                case TOK_RWORD_SELF:
                    pmi.send_rword("self");
                    break;
                case TOK_RWORD_SUPER:
                    pmi.send_rword("super");
                    break;
                case TOK_RWORD_MOVE:
                    pmi.send_rword("move");
                    break;
                case TOK_RWORD_ABSTRACT:
                    pmi.send_rword("abstract");
                    break;
                case TOK_RWORD_FINAL:
                    pmi.send_rword("final");
                    break;
                case TOK_RWORD_OVERRIDE:
                    pmi.send_rword("override");
                    break;
                case TOK_RWORD_VIRTUAL:
                    pmi.send_rword("virtual");
                    break;
                case TOK_RWORD_TYPEOF:
                    pmi.send_rword("typeof");
                    break;
                case TOK_RWORD_BECOME:
                    pmi.send_rword("become");
                    break;
                case TOK_RWORD_UNSIZED:
                    pmi.send_rword("unsized");
                    break;
                case TOK_RWORD_MACRO:
                    pmi.send_rword("macro");
                    break;

                // 2018
                case TOK_RWORD_ASYNC:
                    pmi.send_rword("async");
                    break;
                case TOK_RWORD_AWAIT:
                    pmi.send_rword("await");
                    break;
                case TOK_RWORD_DYN:
                    pmi.send_rword("dyn");
                    break;
                case TOK_RWORD_TRY:
                    pmi.send_rword("try");
                    break;
            }
        }

        void visit_tokentree(const ::TokenTree& tt) {
            if (tt.isToken()) {
                visit_token(tt.tok());
            } else {
                for (size_t i = 0; i < tt.size(); i++) {
                    visit_tokentree(tt[i]);
                }
            }
        }

        void visit_pattern(const ::AST::Pattern& pat) {
            for (const auto& b : pat.bindings()) {
                if (b.isMutable) {
                    pmi.send_rword("mut");
                }
                switch (b.mType) {
                    case ::AST::PatternBinding::Type::MOVE:
                        break;
                    case ::AST::PatternBinding::Type::REF:
                        pmi.send_rword("ref");
                        break;
                    case ::AST::PatternBinding::Type::MUTREF:
                        pmi.send_rword("ref");
                        pmi.send_rword("mut");
                        break;
                }
                if (b.mName == "self") {
                    pmi.send_rword("self");
                    return;
                } else {
                    pmi.send_ident(b.mName);
                }
                pmi.send_symbol("@");
            }
            TU_MATCH_HDRA( (pat.data()), { )
            default:
                TODO(sp, "visit_pattern " << pat.data().tag_str() << " - " << pat);
                TU_ARMA(Any, e) {
                    pmi.send_rword("_");
                }
                TU_ARMA(MaybeBind, e) {
                    if (e.name == "self") {
                        pmi.send_rword("self");
                    } else {
                        pmi.send_ident(e.name);
                    }
                }
                TU_ARMA(Tuple, e) {
                    pmi.send_symbol("(");
                    visit_tuple_pattern(e);
                    pmi.send_symbol(")");
                }
                TU_ARMA(Struct, e) {
                    this->visit_path(e.path);
                    pmi.send_symbol("{");
                    for (const auto& spe : e.sub_patterns) {
                        this->visit_attrs(spe.attrs);
                        pmi.send_ident(spe.name);
                        pmi.send_symbol(":");
                        this->visit_pattern(spe.pat);
                        pmi.send_symbol(",");
                    }
                    if (!e.isExhaustive) {
                        pmi.send_symbol("...");
                    }
                    pmi.send_symbol("}");
                }
            }
        }

        void visit_tuple_pattern(const AST::Pattern::TuplePat& v) {
            for (const auto& p : v.start) {
                visit_pattern(p);
                pmi.send_symbol(",");
            }
            if (v.hasWildcard) {
                pmi.send_symbol("..");
                pmi.send_symbol(",");
                for (const auto& p : v.end) {
                    visit_pattern(p);
                    pmi.send_symbol(",");
                }
            }
        }

        void visit_lifetime(const AST::LifetimeRef& x) {
            if (x.binding() == AST::LifetimeRef::BINDING_STATIC) {
                pmi.send_lifetime("static");
            } else if (x.binding() == AST::LifetimeRef::BINDING_INFER) {
                pmi.send_lifetime("_");
            } else if (x.binding() == AST::LifetimeRef::BINDING_UNSPECIFIED) {
                // Nothing
            } else {
                pmi.send_lifetime(x.name().name.c_str());
            }
        }

        void visit_type(const ::TypeRef& ty) {
            // TODO: Correct handling of visit_type
            TU_MATCHA(
                (ty.mData),
                (te),
                (None, BUG(sp, ty);),
                (Any, pmi.send_rword("_");),
                (Bang, pmi.send_symbol("!");),
                (Unit, pmi.send_symbol("("); pmi.send_symbol(")");),
                (Macro, visit_path(te.inv->path()); pmi.send_symbol("!"); pmi.send_symbol("("); visit_tokentree(te.inv->input_tt()); pmi.send_symbol(")");),
                (Primitive, TODO(sp, "proc_macro send primitive - " << ty);),
                (Function, ::std::stringstream ss; ss << ty << " "; DEBUG("STRING: " << ss.str());

                 parse_string(ss.str());),
                (
                    Tuple, pmi.send_symbol("("); for (const auto& st : te.innerTypes) {
                        this->visit_type(st);
                        pmi.send_symbol(",");
                    } pmi.send_symbol(")");
                ),
                (Borrow, pmi.send_symbol("&"); this->visit_lifetime(te.lifetime); if (te.is_mut) pmi.send_rword("mut"); pmi.send_symbol("("); this->visit_type(*te.inner); pmi.send_symbol(")");),
                (Pointer, pmi.send_symbol("*"); if (te.is_mut) pmi.send_rword("mut"); else pmi.send_rword("const"); pmi.send_symbol("("); this->visit_type(*te.inner); pmi.send_symbol(")");),
                (Array, pmi.send_symbol("["); this->visit_type(*te.inner); pmi.send_symbol(";"); if (te.size) { this->visit_node(*te.size); } else { pmi.send_rword("_"); } pmi.send_symbol("]");),
                (Slice, pmi.send_symbol("["); this->visit_type(*te.inner); pmi.send_symbol("]");),
                (Generic,
                 // TODO: This may already be resolved?... Wait, how?
                 pmi.send_ident(te.name.c_str());),
                (Path, this->visit_path(*te);),
                (
                    TraitObject, pmi.send_symbol("("); pmi.send_rword("dyn"); bool needsPlus = false; for (const auto& t : te.traits) {
                        if (needsPlus) {
                            pmi.send_symbol("+");
                        }
                        needsPlus = true;
                        this->visit_hrbs(t.hrbs);
                        this->visit_bound_constness(t.constness);
                        this->visit_path(*t.path);
                    } for (const auto& lft : te.lifetimes) {
                        if (lft != AST::LifetimeRef()) {
                            if (needsPlus) {
                                pmi.send_symbol("+");
                            }
                            needsPlus = true;
                            this->visit_lifetime(lft);
                        }
                    } pmi.send_symbol(")");
                ),
                (ErasedType, pmi.send_rword("impl"); bool needsPlus = false; for (const auto& t : te->traits) {
                    if (needsPlus) {
                        pmi.send_symbol("+");
                    }
                    needsPlus = true;
                    this->visit_hrbs(t.hrbs);
                    this->visit_bound_constness(t.constness);
                    this->visit_path(*t.path);
                } for (const auto& t : te->maybeTraits) {
                    if (needsPlus) {
                        pmi.send_symbol("+");
                    }
                    needsPlus = true;
                    pmi.send_symbol("?");
                    this->visit_hrbs(t.hrbs);
                    this->visit_path(*t.path);
                } for (const auto& lft : te->lifetimes) {
                    if (needsPlus) {
                        pmi.send_symbol("+");
                    }
                    needsPlus = true;
                    pmi.send_symbol("+");
                    this->visit_lifetime(lft);
                } if (te->use) { TODO(Span(), "`use`"); })
            )
        }

        void visit_hrbs(const AST::HigherRankedBounds& hrbs) {
            if (!hrbs.empty()) {
                pmi.send_rword("for");
                pmi.send_symbol("<");
                for (const auto& v : hrbs.mLifetimes) {
                    pmi.send_lifetime(v.name().name.c_str());
                    pmi.send_symbol(",");
                }
                pmi.send_symbol(">");
            }
        }

        void visit_path_node(const AST::PathNode& e, bool isExpr) {
            pmi.send_ident(e.name().c_str());
            if (!e.args().is_empty()) {
                if (e.args().isParen) {
                    auto& t = e.args().entries.at(0).as_Type();
                    this->visit_type(t); // Should be a tuple
                    auto& rv = e.args().entries.at(1).as_AssociatedTyEqual();
                    pmi.send_symbol("->");
                    this->visit_type(rv.second);
                    return;
                }

                if (isExpr) {
                    pmi.send_symbol("::");
                }
                pmi.send_symbol("<");
                for (const auto& ent : e.args().entries) {
                    TU_MATCH_HDRA( (ent), {)
                    TU_ARMA(Null, _) {
                        }
                        TU_ARMA(Lifetime, l) {
                            pmi.send_lifetime(l.name().name.c_str());
                            pmi.send_symbol(",");
                        }
                        TU_ARMA(Type, t) {
                            this->visit_type(t);
                            pmi.send_symbol(",");
                        }
                        TU_ARMA(Value, n) {
                            pmi.send_symbol("{");
                            this->visit_node(*n);
                            pmi.send_symbol("}");
                            pmi.send_symbol(",");
                        }
                        TU_ARMA(AssociatedTyEqual, a) {
                            visit_path_node(a.first, false);
                            pmi.send_symbol("=");
                            this->visit_type(a.second);
                            pmi.send_symbol(",");
                        }
                        TU_ARMA(AssociatedTyBound, a) {
                            visit_path_node(a.first, false);
                            pmi.send_symbol(":");
                            for (const auto& p : a.second) {
                                if (&p != a.second.data()) {
                                    pmi.send_symbol("+");
                                }
                                this->visit_path(p);
                            }
                            pmi.send_symbol(",");
                        }
                    }
                }
                pmi.send_symbol(">");
            }
        }

        void visit_path(const AST::Path& path, bool isExpr = false) {
            const ::std::vector<AST::PathNode>* nodes = nullptr;
            TU_MATCH_HDRA( (path.cls), {)
            TU_ARMA(Invalid, pe) {
                    BUG(sp, "Invalid path");
                }
                TU_ARMA(Local, pe) {
                    pmi.send_ident(pe.name.c_str());
                }
                TU_ARMA(Relative, pe) {
                    // TODO: Send hygiene information
                    nodes = &pe.nodes;
                }
                TU_ARMA(Self, pe) {
                    pmi.send_rword("self");
                    if (!pe.nodes.empty()) {
                        pmi.send_symbol("::");
                    }
                    nodes = &pe.nodes;
                }
                TU_ARMA(Super, pe) {
                    assert(pe.count > 0);
                    for (unsigned i = 0; i < pe.count; i++) {
                        if (i > 0) {
                            pmi.send_symbol("::");
                        }
                        pmi.send_rword("super");
                    }
                    if (!pe.nodes.empty()) {
                        pmi.send_symbol("::");
                    }
                    nodes = &pe.nodes;
                }
                TU_ARMA(Absolute, pe) {
                    if (pe.crate == "") {
                        pmi.send_rword("crate");
                    } else {
                        pmi.send_symbol("::");
                        //m_pmi.send_string(pe.crate.c_str());
                        assert(pe.crate.c_str()[0] == '=');
                        pmi.send_ident(pe.crate.c_str() + 1);
                    }
                    pmi.send_symbol("::");
                    nodes = &pe.nodes;
                }
                TU_ARMA(UFCS, pe) {
                    pmi.send_symbol("<");
                    this->visit_type(*pe.type);
                    if (pe.trait) {
                        pmi.send_rword("as");
                        this->visit_path(*pe.trait);
                    }
                    pmi.send_symbol(">");
                    pmi.send_symbol("::");
                    nodes = &pe.nodes;
                }
            }
            bool first = true;
            for(const auto& e : *nodes)
            {
                if (!first) {
                    pmi.send_symbol("::");
                }
                first = false;
                visit_path_node(e, isExpr);
            }
        }

        void visit_params(const AST::GenericParams& params) {
            if (!params.mParams.empty()) {
                bool isFirst = true;
                pmi.send_symbol("<");
                for (const auto& param : params.mParams) {
                    if (!isFirst) {
                        pmi.send_symbol(",");
                    }
                    TU_MATCH_HDRA( (param), {)
                    TU_ARMA(None, p) {
                            // Uh... oops?
                            BUG(sp, "Enountered GenericParam::None");
                        }
                        TU_ARMA(Lifetime, p) {
                            pmi.send_lifetime(p.name().name.c_str());
                            bool first = true;
                            for (size_t i = param.boundsStart; i < param.boundsEnd; i++) {
                                if (!params.bounds[i].is_None()) {
                                    if (first) {
                                        pmi.send_symbol(":");
                                        first = false;
                                    } else {
                                        pmi.send_symbol("+");
                                    }
                                }
                            TU_MATCH_HDRA((params.bounds[i]), {)
                            default:
                                BUG(sp, "");
                                    TU_ARMA(None, be) {
                                    }
                                    TU_ARMA(Lifetime, be) {
                                        pmi.send_lifetime(be.test.name().name.c_str());
                                    }
                            }
                            }
                        }
                        TU_ARMA(Type, p) {
                            this->visit_attrs(p.attrs());
                            pmi.send_ident(p.name().c_str());
                            bool first = true;
                            for (size_t i = param.boundsStart; i < param.boundsEnd; i++) {
                                if (!params.bounds[i].is_None()) {
                                    if (first) {
                                        pmi.send_symbol(":");
                                        first = false;
                                    } else {
                                        pmi.send_symbol("+");
                                    }
                                }
                            TU_MATCH_HDRA((params.bounds[i]), {)
                            default:
                                BUG(sp, "Unhandled bound type - " << params.bounds[i]);
                                    TU_ARMA(None, be) {
                                    }
                                    TU_ARMA(TypeLifetime, be) {
                                        pmi.send_lifetime(be.bound.name().name.c_str());
                                    }
                                    TU_ARMA(IsTrait, be) {
                                        assert(be.outer_hrbs.empty()); // Shouldn't be possible in this position
                                        if (!be.innerHrbs.empty()) {
                                            TODO(sp, "be.inner_hrbs");
                                        }
                                        visit_bound_constness(be.constness);
                                        visit_path(be.trait);
                                    }
                                    TU_ARMA(MaybeTrait, be) {
                                        pmi.send_symbol("?");
                                        visit_path(be.trait);
                                    }
                            }
                            }
                            if (!p.getDefault().isWildcard()) {
                                pmi.send_symbol("=");
                                this->visit_type(p.getDefault());
                            }
                        }
                        TU_ARMA(Value, p) {
                            this->visit_attrs(p.attrs());
                            pmi.send_rword("const");
                            pmi.send_ident(p.name().name.c_str());
                            pmi.send_symbol(":");
                            visit_type(p.type());
                            assert(param.boundsStart == param.boundsEnd);
                        }
                    }
                    isFirst = false;
                }
                pmi.send_symbol(">");
            }
        }

        void visit_hrb(const AST::HigherRankedBounds& hrb) {
            if (!hrb.empty()) {
                pmi.send_rword("for");
                pmi.send_symbol("<");
                for (const auto& lft : hrb.mLifetimes) {
                    pmi.send_lifetime(lft.name().name.c_str());
                    pmi.send_symbol(",");
                }
                pmi.send_symbol(">");
            }
        }

        void visit_bounds(const AST::GenericParams& params) {
            if (!params.bounds.empty()) {
                bool where_sent = false;

                for (const auto& e : params.bounds) {
                    size_t i = &e - params.bounds.data();
                    bool alreadyEmitted = false;
                    for (const auto& p : params.mParams) {
                        if (p.is_None()) {
                            continue;
                        }
                        if (p.boundsStart <= i && i < p.boundsEnd) {
                            alreadyEmitted = true;
                        }
                    }
                    if (alreadyEmitted || e.is_None()) {
                        continue;
                    }

                    if (!where_sent) {
                        pmi.send_rword("where");
                        where_sent = true;
                    }
                    TU_MATCH_HDRA((e), {)
                    TU_ARMA(None, be)   continue;
                        TU_ARMA(Lifetime, be) {
                            pmi.send_lifetime(be.bound.name().name.c_str());
                            pmi.send_symbol(":");
                            pmi.send_lifetime(be.test.name().name.c_str());
                        }
                        TU_ARMA(TypeLifetime, be) {
                            visit_type(be.type);
                            pmi.send_symbol(":");
                            pmi.send_lifetime(be.bound.name().name.c_str());
                        }
                        TU_ARMA(IsTrait, be) {
                            visit_hrbs(be.outer_hrbs);
                            visit_type(be.type);
                            pmi.send_symbol(":");
                            visit_hrbs(be.innerHrbs);
                            visit_bound_constness(be.constness);
                            visit_path(be.trait);
                        }
                        TU_ARMA(MaybeTrait, be) {
                            visit_type(be.type);
                            pmi.send_symbol(":");
                            pmi.send_symbol("?");
                            visit_path(be.trait);
                        }
                        TU_ARMA(NotTrait, be) {
                            visit_type(be.type);
                            pmi.send_symbol(":");
                            pmi.send_symbol("!");
                            visit_path(be.trait);
                        }
                        TU_ARMA(Equality, be) {
                            visit_type(be.type);
                            pmi.send_symbol("=");
                            visit_type(be.replacement);
                        }
                    }
                    pmi.send_symbol(",");
                }
            }
        }

        void visit_node(const ::AST::ExprNode& e) {
            DEBUG("NODE: " << e);
            // TODO: Dump to a string, then re-parse into a TT and then send that TT
            // - Avoids needing to repeat logic
            ::std::stringstream ss;
            DumpASTNode(ss, e);
            ss << " ";
            DEBUG("STRING: " << ss.str());

            //const_cast<::AST::ExprNode&>(e).visit(*this);
            parse_string(ss.str());
        }

        void parse_string(const ::std::string& s) {
            ::std::istringstream iss{s};
            Lexer l{iss, AST::Edition::Rust2021, {}};
            for (;;) {
                auto t = l.getToken();
                if (t == TOK_EOF) {
                    break;
                }
                // TODO: If this is an ident, then get the comment after it that specifies the hygine info
                visit_token(t);
            }
        }

        void visit_nodes(const ::AST::Expr& e) {
            this->visit_node(e.node());
        }

        void visit_top_attrs(slice<const ::AST::Attribute>& attrs) {
            for (const auto& a : attrs) {
                this->visit_attr(a);
            }
        }

        void visit_attrs(const ::AST::AttributeList& attrs) {
            for (const auto& a : attrs.mItems) {
                this->visit_attr(a);
            }
        }

        void visit_attr(const ::AST::Attribute& a) {
            if (a.name() == "cfg_attr") {
                auto newAttrs = checkCfgAttr(a);
                for (const auto& na : newAttrs) {
                    this->visit_attr(na);
                }
            }
            if (this->skip_derive_attrs && a.name().is_trivial() && (a.name().asTrivial() == "derive" || a.name().asTrivial() == "derive_const")) {
                DEBUG("Skip " << a << " (derive input)");
                return;
            }
            auto isLocal = (a.name().is_trivial() && pmi.attrIsUsed(a.name().asTrivial()));
            if (this->emitAllAttrs || isLocal) {
                if (isLocal) {
                    a.markInert();
                }
                DEBUG("Send " << a);
                pmi.send_symbol("#");
                pmi.send_symbol("[");
                this->visit_meta_item(a);
                pmi.send_symbol("]");
            } else {
                DEBUG("Skip " << a << " (" << pmi.procMacroDesc.attributes << ")");
            }
        }

        void visit_meta_item(const ::AST::Attribute& i) {
            if (i.name().hasLeading) {
                pmi.send_symbol("::");
            }
            for (const auto& e : i.name().elems) {
                if (&e != &i.name().elems.front()) {
                    pmi.send_symbol("::");
                }
                pmi.send_ident(e.c_str());
            }

            visit_tokentree(i.data());
        }

        void visit_vis(const ::AST::Visibility& vis) {
            switch (vis.ty()) {
                case ::AST::Visibility::Ty::Private:
                    break;
                case ::AST::Visibility::Ty::Pub:
                    pmi.send_rword("pub");
                    break;
                case ::AST::Visibility::Ty::Crate:
                    pmi.send_rword("crate");
                    break;
                case ::AST::Visibility::Ty::PubCrate:
                    pmi.send_rword("pub");
                    pmi.send_symbol("(");
                    pmi.send_rword("crate");
                    pmi.send_symbol(")");
                    break;
                case ::AST::Visibility::Ty::PubSuper:
                    pmi.send_rword("pub");
                    pmi.send_symbol("(");
                    pmi.send_rword("super");
                    pmi.send_symbol(")");
                    break;
                case ::AST::Visibility::Ty::PubSelf:
                    pmi.send_rword("pub");
                    pmi.send_symbol("(");
                    pmi.send_rword("self");
                    pmi.send_symbol(")");
                    break;
                case ::AST::Visibility::Ty::PubIn:
                    pmi.send_rword("pub");
                    pmi.send_symbol("(");
                    pmi.send_rword("in");
                    visit_path(vis.in_path());
                    pmi.send_symbol(")");
                    break;
            }
        }

        void visit_struct(const RcString& name, const AST::Visibility& vis, const ::AST::Struct& str) {
            this->visit_vis(vis);
            pmi.send_rword("struct");
            pmi.send_ident(name.c_str());
            this->visit_params(str.params());
            TU_MATCH_HDRA((str.mData), {)
            TU_ARMA(Unit, se) {
                    this->visit_bounds(str.params());
                    pmi.send_symbol(";");
                }
                TU_ARMA(Tuple, se) {
                    pmi.send_symbol("(");
                    for (const auto& si : se.ents) {
                        this->visit_attrs(si.mAttrs);
                        this->visit_vis(si.vis);
                        this->visit_type(si.mType);
                        pmi.send_symbol(",");
                    }
                    pmi.send_symbol(")");
                    this->visit_bounds(str.params());
                    pmi.send_symbol(";");
                }
                TU_ARMA(Struct, se) {
                    this->visit_bounds(str.params());
                    pmi.send_symbol("{");

                    for (const auto& si : se.ents) {
                        this->visit_attrs(si.mAttrs);
                        this->visit_vis(si.vis);
                        pmi.send_ident(si.mName.c_str());
                        pmi.send_symbol(":");
                        this->visit_type(si.mType);
                        if (si.defaultValue) {
                            pmi.send_symbol("=");
                            this->visit_nodes(si.defaultValue);
                        }
                        pmi.send_symbol(",");
                    }
                    pmi.send_symbol("}");
                }
            }
        }

        void visit_enum(const RcString& name, const AST::Visibility& vis, const ::AST::Enum& enm) {
            this->visit_vis(vis);

            pmi.send_rword("enum");
            pmi.send_ident(name.c_str());
            this->visit_params(enm.params());
            this->visit_bounds(enm.params());
            pmi.send_symbol("{");
            for (const auto& v : enm.variants()) {
                this->visit_attrs(v.mAttrs);
                pmi.send_ident(v.mName.c_str());
                TU_MATCH_HDRA( (v.mData), { )
                TU_ARMA(Unit, e) {
                    }
                    TU_ARMA(Tuple, e) {
                        pmi.send_symbol("(");
                        for (const auto& f : e.mItems) {
                            this->visit_attrs(f.mAttrs);
                            this->visit_type(f.mType);
                            pmi.send_symbol(",");
                        }
                        pmi.send_symbol(")");
                    }
                    TU_ARMA(Struct, e) {
                        pmi.send_symbol("{");
                        for (const auto& f : e.fields) {
                            this->visit_attrs(f.mAttrs);
                            pmi.send_ident(f.mName.c_str());
                            pmi.send_symbol(":");
                            this->visit_type(f.mType);
                            pmi.send_symbol(",");
                        }
                        pmi.send_symbol("}");
                    }
                }
                if( v.discriminantValue)
                {
                    pmi.send_symbol("=");
                    this->visit_nodes(v.discriminantValue);
                }
                pmi.send_symbol(",");
            }
            pmi.send_symbol("}");
        }

        void visit_union(const RcString& name, const AST::Visibility& vis, const ::AST::Union& unn) {
            TODO(sp, "visit_union");
        }

        void visit_function(const RcString& name, const AST::Visibility& vis, const ::AST::Function& fcn) {
            this->visit_vis(vis);

            if (fcn.is_unsafe()) {
                pmi.send_rword("unsafe");
            }
            if (fcn.is_const()) {
                pmi.send_rword("const");
            }
            if (fcn.isAsync()) {
                pmi.send_rword("async");
            }
            if (fcn.abi() != ABI_RUST) {
                pmi.send_rword("extern");
                pmi.send_string(fcn.abi());
            }
            pmi.send_rword("fn");
            pmi.send_ident(name.c_str());
            this->visit_params(fcn.params());
            pmi.send_symbol("(");
            for (const auto& arg : fcn.args()) {
                this->visit_attrs(arg.attrs);
                this->visit_pattern(arg.pat);
                pmi.send_symbol(":");
                this->visit_type(arg.ty);
                pmi.send_symbol(",");
            }
            if (fcn.is_variadic()) {
                pmi.send_symbol("...");
            }
            pmi.send_symbol(")");
            //if( fcn.rettype() != TypeRef() ) {
            pmi.send_symbol("->");
            this->visit_type(fcn.rettype());
            //}
            this->visit_bounds(fcn.params());
            // A trait method declaration has no body - send `;` rather than dereferencing an absent node.
            if (fcn.code().isValid()) {
                this->visit_nodes(fcn.code());
            } else {
                pmi.send_symbol(";");
            }
        }

        void visit_static(const RcString& name, const AST::Visibility& vis, const ::AST::Static& i) {
            this->visit_vis(vis);
            switch (i.s_class()) {
                case ::AST::Static::CONST:
                    pmi.send_rword("const");
                    break;
                case ::AST::Static::MUT:
                    pmi.send_rword("static");
                    pmi.send_rword("mut");
                    break;
                case ::AST::Static::STATIC:
                    pmi.send_rword("static");
                    break;
            }
            pmi.send_ident(name.c_str());
            //this->visit_params(i.params());
            pmi.send_symbol(":");
            this->visit_type(i.type());

            if (i.value()) {
                pmi.send_symbol("=");
                this->visit_node(i.value().node());
            }
            //this->visit_bounds(i.params());
            pmi.send_symbol(";");
        }

        void visit_use(const RcString& /*name*/, const AST::Visibility& vis, const ::AST::UseItem& item) {
            this->visit_vis(vis);
            pmi.send_rword("use");

            if (item.entries.size() == 1) {
                visit_path(item.entries[0].path);
                if (item.entries[0].name == "") {
                    pmi.send_symbol("::");
                    pmi.send_symbol("*");
                } else if (item.entries[0].name != item.entries[0].path.nodes().back().name()) {
                    pmi.send_rword("as");
                    pmi.send_ident(item.entries[0].name.c_str());
                } else {
                }
            } else {
                TODO(sp, "Multiple items");
            }
            pmi.send_symbol(";");
        }

        void visit_impl_hdr(const ::AST::ImplDef& impl) {
            pmi.send_rword("impl");
            if (impl.is_const()) {
                pmi.send_rword("const");
            }
            visit_params(impl.params());

            if (impl.trait().ent.isValid()) {
                visit_path(impl.trait().ent);
                pmi.send_rword("for");
            }
            visit_type(impl.type());
            visit_bounds(impl.params());
        }

        /// Send a trait definition to the proc macro.
        void visit_trait(const RcString& name, const AST::Visibility& vis, const ::AST::Trait& trait) {
            this->visit_vis(vis);
            if (trait.is_unsafe()) {
                pmi.send_rword("unsafe");
            }
            pmi.send_rword("trait");
            pmi.send_ident(name.c_str());
            this->visit_params(trait.params());

            // Supertraits and trait-level lifetime bounds: `trait Foo: Bar + 'a`
            bool first = true;
            for (const auto& st : trait.supertraits()) {
                pmi.send_symbol(first ? ":" : "+");
                first = false;
                this->visit_hrbs(st.ent.hrbs);
                this->visit_bound_constness(st.ent.constness);
                this->visit_path(*st.ent.path);
            }
            for (const auto& lft : trait.lifetimes()) {
                pmi.send_symbol(first ? ":" : "+");
                first = false;
                pmi.send_lifetime(lft.ent.name().name.c_str());
            }
            this->visit_bounds(trait.params());

            pmi.send_symbol("{");
            // Trait items inherit the trait's visibility; mrustc records them as `pub`, which the plugin's parser rejects. Send them unqualified.
            const auto itemVis = ::AST::Visibility::makeBarePrivate();
            for (const auto& i : trait.items()) {
                this->visit_attrs(i.attrs);
                TU_MATCH_HDRA((i.data), {)
                default:
                    TODO(i.span, "visit_trait item - " << i.data.tag_str());
                    break;
                    TU_ARMA(Function, e) {
                        this->visit_function(i.name, itemVis, e);
                    }
                    TU_ARMA(Static, e) {
                        this->visit_static(i.name, itemVis, e);
                    }
                    // An associated type. Bounds live in `m_self_bounds` encoded as `Self: ...`, not the shape needed here, so only the un-bounded form is emitted.
                    TU_ARMA(Type, e) {
                        if (!e.selfBounds.bounds.empty()) {
                            TODO(i.span, "visit_trait - associated type with bounds - " << i.name);
                        }
                        this->visit_vis(itemVis);
                        pmi.send_rword("type");
                        pmi.send_ident(i.name.c_str());
                        this->visit_params(e.mParams);
                        if (e.mType.isValid()) {
                            pmi.send_symbol("=");
                            this->visit_type(e.mType);
                        }
                        pmi.send_symbol(";");
                    }
                }
            }
            pmi.send_symbol("}");
        }

        void visit_impl(const ::AST::Impl& impl) {
            visit_impl_hdr(impl.def());
            pmi.send_symbol("{");
            for (const auto& i : impl.items()) {
                const auto& sp = i.sp;
                const auto& item = *i.data;
                TU_MATCH_HDRA((item), {)
                default:
                    TODO(sp, "Item " << item.tag_str());
                    break;
                    TU_ARMA(Function, e) {
                        visit_function(i.name.c_str(), i.vis, e);
                    }
                    TU_ARMA(Static, e) {
                        visit_static(i.name.c_str(), i.vis, e);
                    }
                }
            }
            pmi.send_symbol("}");
        }

        void visit_item(const RcString& name, const AST::Visibility& vis, const ::AST::Item& item) {
            TU_MATCH_HDRA((item), {)
            default:
                TODO(sp, "visit_item - " << item.tag_str());
                break;
                TU_ARMA(Impl, e) {
                    visit_impl(e);
                }
                TU_ARMA(Use, e) {
                    visit_use(name, vis, e);
                }
                // Types
                TU_ARMA(Struct, e) {
                    visit_struct(name, vis, e);
                }
                TU_ARMA(Enum, e) {
                    visit_enum(name, vis, e);
                }
                TU_ARMA(Union, e) {
                    visit_union(name, vis, e);
                }
                TU_ARMA(Trait, e) {
                    visit_trait(name, vis, e);
                }

                // Values
                TU_ARMA(Function, e) {
                    visit_function(name, vis, e);
                }
            }
        }
    };
}

::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& macPath, const TokenTree* attrInput, std::function<void(Visitor& v)> cb) {
    // 1. Create ProcMacroInv instance
    auto pmi = ProcMacroInvokeInt(sp, crate, macPath);
    if (!pmi.checkGood()) {
        return ::std::unique_ptr<TokenStream>();
    }
    if (attrInput) {
        // TODO: Assert that this is a `#[proc_macro_attribute]` macro
        if (attrInput->size() != 0) {
            // If the input is non-empty, then it must be a parenthesised token tree
            ASSERT_BUG(sp, attrInput->size() >= 2, "");
            ASSERT_BUG(sp, (*attrInput)[0].tok() == TOK_PAREN_OPEN || (*attrInput)[0].tok() == TOK_SQUARE_OPEN, "");
            Visitor v(sp, pmi);
            // - Strip the parens when sending
            for (size_t i = 1; i < attrInput->size() - 1; i++) {
                v.visit_tokentree((*attrInput)[i]);
            }
        }
        pmi.send_done();
    }
    // 2. Feed item as a token stream.
    Visitor v(sp, pmi);
    cb(v);
    pmi.send_done();
    // 3. Return boxed invocation instance
    return box$(pmi);
}

// --- Derive inputs
::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& macPath, slice<const AST::Attribute> attrs, const AST::Visibility& vis, const RcString& itemName, const ::AST::Struct& i) {
    return ProcMacroInvoke(sp, crate, macPath, nullptr, [&](Visitor& v) {
        DEBUG("derive on struct");
        v.skip_derive_attrs = true;
        v.visit_top_attrs(attrs);
        v.visit_struct(itemName, vis, i);
    });
}

::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& macPath, slice<const AST::Attribute> attrs, const AST::Visibility& vis, const RcString& itemName, const ::AST::Enum& i) {
    return ProcMacroInvoke(sp, crate, macPath, nullptr, [&](Visitor& v) {
        DEBUG("derive on enum");
        v.skip_derive_attrs = true;
        v.visit_top_attrs(attrs);
        v.visit_enum(itemName, vis, i);
    });
}

::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& macPath, slice<const AST::Attribute> attrs, const AST::Visibility& vis, const RcString& itemName, const ::AST::Union& i) {
    return ProcMacroInvoke(sp, crate, macPath, nullptr, [&](Visitor& v) {
        DEBUG("derive on union");
        v.skip_derive_attrs = true;
        v.visit_top_attrs(attrs);
        v.visit_union(itemName, vis, i);
    });
}

// --- attribute
::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& macPath, const TokenTree& tt, slice<const AST::Attribute> attrs, const AST::Visibility& vis, const RcString& itemName, const ::AST::Item& i) {
    return ProcMacroInvoke(sp, crate, macPath, &tt, [&](Visitor& v) {
        v.emitAllAttrs = true;
        v.visit_top_attrs(attrs);
        v.visit_item(itemName, vis, i);
    });
}

// -- function-like input
::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ::AST::Crate& crate, const ::std::vector<RcString>& macPath, const TokenTree& tt) {
    return ProcMacroInvoke(sp, crate, macPath, nullptr, [&](Visitor& v) {
        v.visit_tokentree(tt);
    });
}

ProcMacroInv::ProcMacroInv(const Span& sp, AST::Edition edition, const char* executable, const ::HIR::ProcMacro& proc_macro_desc)
    : TokenStream(ParseState())
    , parentSpan(sp)
    , thisSpan(Span(parentSpan, proc_macro_desc.path.crate_name(), proc_macro_desc.name))
    , procMacroDesc(proc_macro_desc)
    , edition(edition)
{
    if (getenv("MRUSTC_DUMP_PROCMACRO") && getenv("MRUSTC_DUMP_PROCMACRO")[0]) {
        // TODO: Dump both input and output, AND (optionally) dump each invocation
        static unsigned int dumpCount = 0;
        std::string namePrefix;
        namePrefix = FMT(getenv("MRUSTC_DUMP_PROCMACRO") << "-" << dumpCount);
        DEBUG("Dumping to " << namePrefix);
        dumpFileOut.open(FMT(namePrefix << "-out.bin"), ::std::ios::out | ::std::ios::binary);
        dumpFileRes.open(FMT(namePrefix << "-res.bin"), ::std::ios::out | ::std::ios::binary);
        dumpCount++;
    } else {
        DEBUG("Set MRUSTC_DUMP_PROCMACRO=procmacro_dump to dump to `procmacro_dump-NNN-{out,res}.bin`");
    }
    int stdin_pipes[2];
    if (pipe(stdin_pipes) != 0) {
        BUG(sp, "Unable to create stdin pipe pair for proc macro, " << strerror(errno));
    }
    this->handles.childStdin = stdin_pipes[1]; // Write end
    int stdout_pipes[2];
    if (pipe(stdout_pipes) != 0) {
        BUG(sp, "Unable to create stdout pipe pair for proc macro, " << strerror(errno));
    }
    this->handles.childStdout = stdout_pipes[0]; // Read end

    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_adddup2(&file_actions, stdin_pipes[0], 0);
    posix_spawn_file_actions_adddup2(&file_actions, stdout_pipes[1], 1);
    posix_spawn_file_actions_addclose(&file_actions, stdin_pipes[0]);
    posix_spawn_file_actions_addclose(&file_actions, stdin_pipes[1]);
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipes[0]);
    posix_spawn_file_actions_addclose(&file_actions, stdout_pipes[1]);

    char* argv[3] = {const_cast<char*>(executable), const_cast<char*>(proc_macro_desc.name.c_str()), nullptr};
    DEBUG(argv[0] << " " << argv[1]);
    //char*   envp[] = { nullptr };
    int rv = posix_spawn(&this->handles.childPid, executable, &file_actions, nullptr, argv, environ);
    if (rv != 0) {
        BUG(sp, "Error in posix_spawn - " << rv << " - can't start `" << executable << "`");
    }

    posix_spawn_file_actions_destroy(&file_actions);
    // Close the ends we don't care about.
    close(stdin_pipes[0]);
    close(stdout_pipes[1]);

    // Invocation span is #1 (#0 is always empty/undefined)
    this->send_span_def(1, sp);
}

ProcMacroInv::Handles::Handles(Handles&& x)
    : childPid(x.childPid)
    , childStdin(x.childStdin)
    , childStdout(x.childStdout)
{
    x.childPid = 0;
    x.childStdin = -1;
    x.childStdout = -1;
    DEBUG("");
}
ProcMacroInv::~ProcMacroInv()
{
    if (this->handles.childPid != 0) {
        DEBUG("Waiting for child " << this->handles.childPid << " to terminate");
        int status;
        waitpid(this->handles.childPid, &status, 0);
        close(this->handles.childStdout);
        close(this->handles.childStdin);
    }
}

bool ProcMacroInv::checkGood() {
    char v;
    int rv = read(this->handles.childStdout, &v, 1);
    if (rv == 0) {
        DEBUG("Unexpected EOF from child");
        return false;
    }
    if (rv < 0) {
        DEBUG("Error reading from child, rv=" << rv << " " << strerror(errno));
        return false;
    }
    DEBUG("Child started, value = " << (int)v);
    if (v != 0) {
        return false;
    }
    return true;
}

void ProcMacroInv::send_u8(uint8_t v) {
    this->send_bytes_raw(&v, 1);
}

void ProcMacroInv::send_bytes(const void* val, size_t size) {
    this->send_v128u(static_cast<uint64_t>(size));
    this->send_bytes_raw(val, size);
}

void ProcMacroInv::send_bytes_raw(const void* val, size_t size) {
    if (dumpFileOut.is_open()) {
        dumpFileOut.write(reinterpret_cast<const char*>(val), size);
    }
    if (write(this->handles.childStdin, val, size) != static_cast<ssize_t>(size)) {
        BUG(parentSpan, "Error writing to child, " << strerror(errno));
    }
}

void ProcMacroInv::send_v128u(uint64_t val) {
    while (val >= 128) {
        this->send_u8(static_cast<uint8_t>(val & 0x7F) | 0x80);
        val >>= 7;
    }
    this->send_u8(static_cast<uint8_t>(val & 0x7F));
}

void ProcMacroInv::send_v128u(U128 val) {
    while (val >= U128(128)) {
        this->send_u8(static_cast<uint8_t>(val.truncate_u64() & 0x7F) | 0x80);
        val >>= 7;
    }
    this->send_u8(static_cast<uint8_t>(val.truncate_u64() & 0x7F));
}

uint8_t ProcMacroInv::recv_u8() {
    uint8_t v;
    this->recv_bytes_raw(&v, 1);
    return v;
}

::std::string ProcMacroInv::recv_bytes() {
    auto len = this->recv_v128u();
    ASSERT_BUG(this->parentSpan, len < SIZE_MAX, "Oversized string from child process");
    ::std::string val;
    val.resize(len);

    recv_bytes_raw(&val[0], len);

    return val;
}

void ProcMacroInv::recv_bytes_raw(void* out_void, size_t len) {
    uint8_t* val = reinterpret_cast<uint8_t*>(out_void);
    size_t ofs = 0, rem = len;
    while (rem > 0) {
        auto n = read(this->handles.childStdout, &val[ofs], rem);
        if (n == 0) {
            BUG(this->thisSpan, "Unexpected EOF while reading from child process");
        }
        if (n < 0) {
            BUG(this->parentSpan, "Error while reading from child process");
        }
        assert(static_cast<size_t>(n) <= rem);
        ofs += n;
        rem -= n;
    }

    if (dumpFileRes.is_open()) {
        dumpFileRes.write(reinterpret_cast<const char*>(out_void), len);
        dumpFileRes.flush();
    }
}

uint64_t ProcMacroInv::recv_v128u() {
    uint64_t v = 0;
    unsigned ofs = 0;
    for (;;) {
        auto b = recv_u8();
        v |= static_cast<uint64_t>(b & 0x7F) << ofs;
        if ((b & 0x80) == 0) {
            break;
        }
        ofs += 7;
    }
    return v;
}

U128 ProcMacroInv::recv_v128u_u128() {
    U128 v(0);
    unsigned ofs = 0;
    for (;;) {
        auto b = recv_u8();
        v |= U128(b & 0x7F) << ofs;
        if ((b & 0x80) == 0) {
            break;
        }
        ofs += 7;
    }
    return v;
}

Position ProcMacroInv::getPosition() const {
    //DEBUG("" << m_this_span);
    //return Position(m_this_span);
    return Position(parentSpan);
}

Token ProcMacroInv::realGetToken() {
    auto rv = this->realGetToken_();
    DEBUG("ProcMacroInv: " << rv);
    return rv;
}

Token ProcMacroInv::realGetToken_() {
    if (eofHit) {
        return Token(TOK_EOF);
    }
    uint8_t v = this->recv_u8();

    switch (static_cast<TokenClass>(v)) {
        case TokenClass::EndOfStream:
            TODO(this->parentSpan, "EndOfStream");
        case TokenClass::SpanRef:
            TODO(this->parentSpan, "SpanDef");
        case TokenClass::SpanDef:
            TODO(this->parentSpan, "SpanDef");
            break;
        case TokenClass::Symbol: {
            auto val = this->recv_bytes();
            if (val == "") {
                eofHit = true;
                return Token(TOK_EOF);
            }
            auto t = LexFindOperator(val);
            ASSERT_BUG(this->parentSpan, t != TOK_NULL, "Unknown symbol from child process - '" << val << "'");
            return t;
        }
        case TokenClass::Ident: {
            auto val = this->recv_bytes();
            if (val == "_" || val == "r#_") {
                return TOK_UNDERSCORE;
            }
            auto t = LexFindReservedWord(val, edition);
            if (t != TOK_NULL) {
                return t;
            }
            if (val[0] == 'r' && val[1] == '#') {
                return Token(TOK_IDENT, RcString::newInterned(val.c_str() + 2));
            }
            return Token(TOK_IDENT, RcString::newInterned(val));
        }
        case TokenClass::Lifetime: {
            auto val = this->recv_bytes();
            return Token(TOK_LIFETIME, RcString::newInterned(val));
        }
        case TokenClass::String: {
            auto val = this->recv_bytes();
            return Token(TOK_STRING, mv$(val), this->getHygiene());
        }
        case TokenClass::ByteString: {
            auto val = this->recv_bytes();
            return Token(TOK_BYTESTRING, mv$(val), this->getHygiene());
        }
        case TokenClass::CharLit: {
            auto val = this->recv_v128u();
            return Token(U128(val), CORETYPE_CHAR);
        }
        case TokenClass::UnsignedInt: {
            ::eCoreType ty;
            switch (this->recv_u8()) {
                case 0:
                    ty = CORETYPE_ANY;
                    break;
                case 1:
                    ty = CORETYPE_UINT;
                    break;
                case 8:
                    ty = CORETYPE_U8;
                    break;
                case 16:
                    ty = CORETYPE_U16;
                    break;
                case 32:
                    ty = CORETYPE_U32;
                    break;
                case 64:
                    ty = CORETYPE_U64;
                    break;
                case 128:
                    ty = CORETYPE_U128;
                    break;
                default:
                    BUG(this->parentSpan, "Invalid integer size from child process");
            }
            auto val = this->recv_v128u_u128();
            return Token(val, ty);
        }
        case TokenClass::SignedInt: {
            ::eCoreType ty;
            switch (this->recv_u8()) {
                case 0:
                    ty = CORETYPE_ANY;
                    break;
                case 1:
                    ty = CORETYPE_INT;
                    break;
                case 8:
                    ty = CORETYPE_I8;
                    break;
                case 16:
                    ty = CORETYPE_I16;
                    break;
                case 32:
                    ty = CORETYPE_I32;
                    break;
                case 64:
                    ty = CORETYPE_I64;
                    break;
                case 128:
                    ty = CORETYPE_I128;
                    break;
                default:
                    BUG(this->parentSpan, "Invalid integer size from child process");
            }
            auto val = this->recv_v128u_u128();
            if (val.truncate_u64() & 1) {
                val = ~(val >> 1) + 1; // Negative (Is this even possible?)
                TODO(this->parentSpan, "Negative literal from proc macro, what?");
            } else {
                val = (val >> 1);
            }
            return Token(val, ty);
        }
        case TokenClass::Float: {
            ::eCoreType ty;
            switch (this->recv_u8()) {
                case 0:
                    ty = CORETYPE_ANY;
                    break;
                case 32:
                    ty = CORETYPE_F32;
                    break;
                case 64:
                    ty = CORETYPE_F64;
                    break;
                default:
                    BUG(this->parentSpan, "Invalid float size from child process");
            }
            double val;
            this->recv_bytes_raw(&val, sizeof(val));
            return Token::makeFloat(val, ty);
        }
            //case TokenClass::Fragment:
            //    TODO(this->m_parent_span, "Handle ints/floats/fragments from child process");
    }
    BUG(this->parentSpan, "Invalid token class from child process - " << int(v));

    throw "";
}

Ident::Hygiene ProcMacroInv::realGetHygiene() const {
    return Ident::Hygiene();
}
