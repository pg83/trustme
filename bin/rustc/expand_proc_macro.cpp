#include "expand_proc_macro.h"

#include "common.h"
#include "synext.h"
#include "ast_ast.h"
#include "hir_hir.h" // ABI_RUST
#include "ast_dump.h"
#include "ast_expr.h"
#include "ast_crate.h"
#include "parse_lex.h"
#include "expand_cfg.h"
#include "main_bindings.h"
#include "parse_ttstream.h"

#include <spawn.h>
#include <unistd.h> // read/write/pipe
#include <sys/wait.h>
#include <unordered_set>

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__APPLE__)
extern char** environ;
#endif

#define NEWNODE(ty, ...) ASTExprNodeP(new ASTExprNode##ty(__VA_ARGS__))

class DecoratorProcMacroDerive: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& attr, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (i.is_None()) {
            return;
        }

        if (!i.is_Function()) {
            TODO(sp, "Error for proc_macro_derive on non-Function");
        }

        ::std::vector<::std::string> attributes;
        TTStream lex(sp, ParseState(), attr.data());
        lex.getTokenCheck(TOK_PAREN_OPEN);
        auto traitName = lex.getTokenCheck(TOK_IDENT).ident().name;
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

        crate.procMacros.push_back(ASTProcMacroDef{ASTProcMacroTy::Derive, RcString::newInterned(FMT(traitName)), path, mv$(attributes)});
    }
};
STATIC_DECORATOR("proc_macro_derive", DecoratorProcMacroDerive)

class DecoratorProcMacroAttribute: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& attr, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (i.is_None()) {
            return;
        }

        if (!i.is_Function()) {
            TODO(sp, "Error for #[proc_macro_attribute] on non-Function");
        }

        crate.procMacros.push_back(ASTProcMacroDef{ASTProcMacroTy::Attribute, path.nodes.back(), path, {}});
    }
};
STATIC_DECORATOR("proc_macro_attribute", DecoratorProcMacroAttribute)

class DecoratorProcMacro: public ExpandDecorator {
public:
    AttrStage stage() const override {
        return AttrStage::Post;
    }

    void handle(const Span& sp, const ASTAttribute& attr, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override {
        if (i.is_None()) {
            return;
        }

        if (!i.is_Function()) {
            TODO(sp, "Error for #[proc_macro] on non-Function");
        }

        crate.procMacros.push_back(ASTProcMacroDef{ASTProcMacroTy::Function, path.nodes.back(), path, {}});
    }
};
STATIC_DECORATOR("proc_macro", DecoratorProcMacro)

void ExpandProcMacroHarness(ASTCrate& crate) {
    auto pmCrateName = RcString::newInterned("proc_macro");
    gImplicitCrates.insert(std::make_pair(pmCrateName, crate.loadExternCrate(Span(), pmCrateName)));

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
    auto mainFn = ASTFunction{Span(), TypeRef(TypeRef::TagUnit(), Span()), {}};
    {
        auto callNode = NEWNODE(CallPath, ASTPath(crate.extCratenameProcmacro, {ASTPathNode("main")}), ::makeVec1(NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(NamedValue, ASTPath("", {ASTPathNode("proc_macro#"), ASTPathNode("MACROS")})))));
        mainFn.setCode(mv$(callNode));
    }

    // ---- test list ----
    ::std::vector<ASTExprNodeP> testNodes;

    for (const auto& desc : crate.procMacros) {
        const char* typeName = "SingleStream";
        switch (desc.ty) {
            case ASTProcMacroTy::Attribute:
                typeName = "Attribute";
                break;
            default:
                break;
        }
        ASTExprNodeStructLiteral::tValues descVals;
        // `name: "foo",`
        descVals.push_back({{}, "name", NEWNODE(String, desc.name.c_str())});
        // `handler`: ::foo
        descVals.push_back({{}, "handler", NEWNODE(CallPath, ASTPath(crate.extCratenameProcmacro, {ASTPathNode("MacroType"), ASTPathNode(typeName)}), ::makeVec1(NEWNODE(NamedValue, ASTPath(desc.path))))});

        testNodes.push_back(NEWNODE(StructLiteral, ASTPath(crate.extCratenameProcmacro, {ASTPathNode("MacroDesc")}), nullptr, mv$(descVals)));
    }
    auto* testsArray = new ASTExprNodeArray(mv$(testNodes));

    size_t testCount = testsArray->values.size();
    auto testsList = ASTStatic{ASTStatic::Class::STATIC, TypeRef(TypeRef::TagSizedArray(), Span(), TypeRef(Span(), ASTPath(crate.extCratenameProcmacro, {ASTPathNode("MacroDesc")})), ::std::shared_ptr<ASTExprNode>(new ASTExprNodeInteger(U128(testCount), CORETYPE_UINT))), ASTExpr(mv$(testsArray))};

    // ---- module ----
    auto newmod = ASTModule{ASTAbsolutePath("", {"proc_macro#"})};
    // - TODO: These need to be loaded too.
    //  > They don't actually need to exist here, just be loaded (and use absolute paths)
    auto visPrivate = ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, newmod.path());
    newmod.addExtCrate(Span(), visPrivate, crate.extCratenameProcmacro, "proc_macro", {});

    newmod.addItem(Span(), visPrivate, "main", mv$(mainFn), {});
    newmod.addItem(Span(), visPrivate, "MACROS", mv$(testsList), {});

    crate.mRootModule.addItem(Span(), visPrivate, "proc_macro#", mv$(newmod), {});
    crate.mLangItems["mrustc-main"] = ASTAbsolutePath("", {"proc_macro#", "main"});
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
    const HIRProcMacro& procMacroDesc;
    ASTEdition edition;
    ::std::ofstream dumpFileOut;
    ::std::ofstream dumpFileRes;

    /// Spans that have had an index assigned
    ::std::unordered_map<const SpanInner*, size_t> knownSpans;
    /// Span indexes that have been sent
    ::std::unordered_set<size_t> sentSpans;
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
    ProcMacroInv(const Span& sp, ASTEdition edition, const char* executable, const HIRProcMacro& procMacroDesc);
    ProcMacroInv(const ProcMacroInv&) = delete;
    ProcMacroInv(ProcMacroInv&&) = default;
    ProcMacroInv& operator=(const ProcMacroInv&) = delete;
    ProcMacroInv& operator=(ProcMacroInv&&) = delete;
    ~ProcMacroInv();

    bool checkGood();

    void sendDone() {
        this->sendU8(static_cast<uint8_t>(TokenClass::EndOfStream));
        dumpFileOut.flush();
        DEBUG("Input tokens sent");
    }

    void sendSymbol(const char* val) {
        this->sendU8(static_cast<uint8_t>(TokenClass::Symbol));
        this->sendBytes(val, ::std::strlen(val));
    }

    void sendRword(const char* val) {
        this->sendU8(static_cast<uint8_t>(TokenClass::Ident));
        this->sendBytes(val, ::std::strlen(val));
    }

    void sendIdent(const char* val) {
        this->sendU8(static_cast<uint8_t>(TokenClass::Ident));
        if (LexFindReservedWord(val, edition) != TOK_NULL) {
            auto size = ::std::strlen(val);
            this->sendV128u(2 + size);
            this->sendBytesRaw("r#", 2);
            this->sendBytesRaw(val, size);
        } else {
            this->sendBytes(val, ::std::strlen(val));
        }
    }

    void sendIdent(const Ident& val) {
        sendIdent(val.name.c_str());
    }

    void sendLifetime(const char* val) {
        this->sendU8(static_cast<uint8_t>(TokenClass::Lifetime));
        this->sendBytes(val, ::std::strlen(val));
    }

    void sendString(const ::std::string& s) {
        this->sendU8(static_cast<uint8_t>(TokenClass::String));
        this->sendBytes(s.data(), s.size());
    }

    void sendBytestring(const ::std::string& s) {
        this->sendU8(static_cast<uint8_t>(TokenClass::ByteString));
        this->sendBytes(s.data(), s.size());
    }

    void sendChar(uint32_t ch) {
        this->sendU8(static_cast<uint8_t>(TokenClass::CharLit));
        this->sendV128u(ch);
    }

    void sendInt(eCoreType ct, U128 v) {
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
                this->sendU8(static_cast<uint8_t>(TokenClass::UnsignedInt));
                this->sendU8(size);
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
                this->sendU8(static_cast<uint8_t>(TokenClass::SignedInt));
                this->sendU8(size);
                break;
            default:
                BUG(parentSpan, "Unknown integer type");
        }
        this->sendV128u(v);
    }

    void sendFloat(eCoreType ct, FloatValue v) {
        this->sendU8(static_cast<uint8_t>(TokenClass::Float));
        switch (ct) {
            case CORETYPE_ANY:
                this->sendU8(0);
                break;
            case CORETYPE_F32:
                this->sendU8(32);
                break;
            case CORETYPE_F64:
                this->sendU8(64);
                break;
            default:
                BUG(parentSpan, "Unknown float type");
        }
        double wireValue = static_cast<double>(v);
        this->sendBytesRaw(&wireValue, sizeof(wireValue));
    }

    void sendSpanDef(size_t index, const Span& sp) {
        this->knownSpans[sp.get()] = index;
        this->sentSpans.insert(index);

        this->sendU8(static_cast<uint8_t>(TokenClass::SpanDef));
        this->sendV128u(index);
        this->sendV128u(0); // TODO: Parent span
        if (const auto* spP = cast<const SpanInnerSource>(sp.get())) {
            this->sendBytes(spP->filename.c_str(), spP->filename.size());
            this->sendU8(1); // path_is_real
            this->sendV128u(spP->startLine);
            this->sendV128u(spP->endLine);
            this->sendV128u(spP->startOfs);
            this->sendV128u(spP->endOfs);
        } else {
            this->sendBytes("MACRO", 5); // TODO: better filename?
            this->sendU8(0);             // path_is_real
            this->sendV128u(0);
            this->sendV128u(0);
            this->sendV128u(0);
            this->sendV128u(0);
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

    virtual ASTEdition realGetEdition() const override {
        return edition;
    }

    virtual Ident::Hygiene realGetHygiene() const override;

private:
    Token realGetToken_();
    void sendU8(uint8_t v);
    void sendBytes(const void* val, size_t size);
    void sendBytesRaw(const void* val, size_t size);
    void sendV128u(uint64_t val);
    void sendV128u(U128 val);

    uint8_t recvU8();
    ::std::string recvBytes();
    void recvBytesRaw(void* outVoid, size_t len);
    uint64_t recvV128u();
    U128 recvV128uU128();
};

ProcMacroInv ProcMacroInvokeInt(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath) {
    TRACE_FUNCTION_F(macPath);
    // 1. Locate macro in HIR list
    const auto& crateName = macPath.front();
    ASSERT_BUG(sp, crate.externCrates.count(crateName), "Crate not loaded for macro: [" << macPath << "]");
    const auto& extCrate = crate.externCrates.at(crateName);
    // TODO: Ensure that this macro is in the listed crate.
    const HIRProcMacro* pmp = nullptr;
    for (const auto& mi : extCrate.hir->mRootModule.macroItems) {
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
    ::std::string procMacroExeName = extCrate.filename;

    // 3. Create ProcMacroInv
    auto rv = ProcMacroInv(sp, extCrate.hir->edition, procMacroExeName.c_str(), *pmp);
    rv.parseState().crate = &crate;

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
        bool skipDeriveAttrs = false;

        Visitor(const Span& sp, ProcMacroInv& pmi)
            : sp(sp)
            , pmi(pmi)
            //,emit_all_attrs(false)
            , emitAllAttrs(true)
        {
        }

        void visitBoundConstness(ASTBoundConstness constness) {
            if (constness == ASTBoundConstness::Always) {
                pmi.sendRword("const");
            } else if (constness == ASTBoundConstness::Maybe) {
                pmi.sendSymbol("[");
                pmi.sendRword("const");
                pmi.sendSymbol("]");
            }
        }

        void visitToken(const ::Token& tok) {
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
                    visitType(const_cast<::Token&>(tok).fragType());
                    break;
                case TOK_INTERPOLATED_PATH:
                    TODO(sp, "TOK_INTERPOLATED_PATH");
                case TOK_INTERPOLATED_PATTERN:
                    TODO(sp, "TOK_INTERPOLATED_PATTERN");
                case TOK_INTERPOLATED_STMT:
                case TOK_INTERPOLATED_BLOCK:
                case TOK_INTERPOLATED_EXPR:
                    visitNode(const_cast<::Token&>(tok).fragNode());
                    break;
                case TOK_INTERPOLATED_META:
                case TOK_INTERPOLATED_STMT_ITEM:
                case TOK_INTERPOLATED_ITEM:
                case TOK_INTERPOLATED_VIS:
                    TODO(sp, "TOK_INTERPOLATED_...");
                // Value tokens
                case TOK_IDENT:
                    pmi.sendIdent(tok.ident().name.c_str());
                    break; // TODO: Raw idents
                case TOK_LIFETIME:
                    pmi.sendLifetime(tok.ident().name.c_str());
                    break; // TODO: Hygine?
                case TOK_INTEGER:
                    if (tok.datatype() == CORETYPE_CHAR) {
                        pmi.sendChar(tok.intval().truncateU64());
                    } else {
                        pmi.sendInt(tok.datatype(), tok.intval());
                    }
                    break;
                case TOK_CHAR:
                    pmi.sendChar(tok.intval().truncateU64());
                    break;
                case TOK_FLOAT:
                    pmi.sendFloat(tok.datatype(), tok.floatval());
                    break;
                case TOK_STRING:
                    pmi.sendString(tok.str());
                    break;
                case TOK_BYTESTRING:
                    pmi.sendBytestring(tok.str());
                    break;
                case TOK_CSTRING:
                    TODO(sp, "TOK_CSTRING");

                case TOK_HASH:
                    pmi.sendSymbol("#");
                    break;
                case TOK_UNDERSCORE:
                    pmi.sendRword("_");
                    break;

                // Symbols
                case TOK_PAREN_OPEN:
                    pmi.sendSymbol("(");
                    break;
                case TOK_PAREN_CLOSE:
                    pmi.sendSymbol(")");
                    break;
                case TOK_BRACE_OPEN:
                    pmi.sendSymbol("{");
                    break;
                case TOK_BRACE_CLOSE:
                    pmi.sendSymbol("}");
                    break;
                case TOK_LT:
                    pmi.sendSymbol("<");
                    break;
                case TOK_GT:
                    pmi.sendSymbol(">");
                    break;
                case TOK_SQUARE_OPEN:
                    pmi.sendSymbol("[");
                    break;
                case TOK_SQUARE_CLOSE:
                    pmi.sendSymbol("]");
                    break;
                case TOK_COMMA:
                    pmi.sendSymbol(",");
                    break;
                case TOK_SEMICOLON:
                    pmi.sendSymbol(";");
                    break;
                case TOK_COLON:
                    pmi.sendSymbol(":");
                    break;
                case TOK_DOUBLE_COLON:
                    pmi.sendSymbol("::");
                    break;
                case TOK_STAR:
                    pmi.sendSymbol("*");
                    break;
                case TOK_AMP:
                    pmi.sendSymbol("&");
                    break;
                case TOK_PIPE:
                    pmi.sendSymbol("|");
                    break;

                case TOK_FATARROW:
                    pmi.sendSymbol("=>");
                    break;
                case TOK_THINARROW:
                    pmi.sendSymbol("->");
                    break;
                case TOK_THINARROW_LEFT:
                    pmi.sendSymbol("<-");
                    break;

                case TOK_PLUS:
                    pmi.sendSymbol("+");
                    break;
                case TOK_DASH:
                    pmi.sendSymbol("-");
                    break;
                case TOK_EXCLAM:
                    pmi.sendSymbol("!");
                    break;
                case TOK_PERCENT:
                    pmi.sendSymbol("%");
                    break;
                case TOK_SLASH:
                    pmi.sendSymbol("/");
                    break;

                case TOK_DOT:
                    pmi.sendSymbol(".");
                    break;
                case TOK_DOUBLE_DOT:
                    pmi.sendSymbol("..");
                    break;
                case TOK_DOUBLE_DOT_EQUAL:
                    pmi.sendSymbol("..=");
                    break;
                case TOK_TRIPLE_DOT:
                    pmi.sendSymbol("...");
                    break;

                case TOK_EQUAL:
                    pmi.sendSymbol("=");
                    break;
                case TOK_PLUS_EQUAL:
                    pmi.sendSymbol("+=");
                    break;
                case TOK_DASH_EQUAL:
                    pmi.sendSymbol("-");
                    break;
                case TOK_PERCENT_EQUAL:
                    pmi.sendSymbol("%=");
                    break;
                case TOK_SLASH_EQUAL:
                    pmi.sendSymbol("/=");
                    break;
                case TOK_STAR_EQUAL:
                    pmi.sendSymbol("*=");
                    break;
                case TOK_AMP_EQUAL:
                    pmi.sendSymbol("&=");
                    break;
                case TOK_PIPE_EQUAL:
                    pmi.sendSymbol("|=");
                    break;

                case TOK_DOUBLE_EQUAL:
                    pmi.sendSymbol("==");
                    break;
                case TOK_EXCLAM_EQUAL:
                    pmi.sendSymbol("!=");
                    break;
                case TOK_GTE:
                    pmi.sendSymbol(">=");
                    break;
                case TOK_LTE:
                    pmi.sendSymbol("<=");
                    break;

                case TOK_DOUBLE_AMP:
                    pmi.sendSymbol("&&");
                    break;
                case TOK_DOUBLE_PIPE:
                    pmi.sendSymbol("||");
                    break;
                case TOK_DOUBLE_LT:
                    pmi.sendSymbol("<<");
                    break;
                case TOK_DOUBLE_GT:
                    pmi.sendSymbol(">>");
                    break;
                case TOK_DOUBLE_LT_EQUAL:
                    pmi.sendSymbol("<=");
                    break;
                case TOK_DOUBLE_GT_EQUAL:
                    pmi.sendSymbol(">=");
                    break;

                case TOK_DOLLAR:
                    pmi.sendSymbol("$");
                    break;

                case TOK_QMARK:
                    pmi.sendSymbol("?");
                    break;
                case TOK_AT:
                    pmi.sendSymbol("@");
                    break;
                case TOK_TILDE:
                    pmi.sendSymbol("~");
                    break;
                case TOK_BACKSLASH:
                    pmi.sendSymbol("\\");
                    break;
                case TOK_CARET:
                    pmi.sendSymbol("^");
                    break;
                case TOK_CARET_EQUAL:
                    pmi.sendSymbol("^=");
                    break;
                case TOK_BACKTICK:
                    pmi.sendSymbol("`");
                    break;

                // Reserved Words
                case TOK_RWORD_PUB:
                    pmi.sendRword("pub");
                    break;
                case TOK_RWORD_PRIV:
                    pmi.sendRword("priv");
                    break;
                case TOK_RWORD_MUT:
                    pmi.sendRword("mut");
                    break;
                case TOK_RWORD_CONST:
                    pmi.sendRword("const");
                    break;
                case TOK_RWORD_STATIC:
                    pmi.sendRword("static");
                    break;
                case TOK_RWORD_UNSAFE:
                    pmi.sendRword("unsafe");
                    break;
                case TOK_RWORD_EXTERN:
                    pmi.sendRword("extern");
                    break;
                case TOK_RWORD_CRATE:
                    pmi.sendRword("crate");
                    break;
                case TOK_RWORD_MOD:
                    pmi.sendRword("mod");
                    break;
                case TOK_RWORD_STRUCT:
                    pmi.sendRword("struct");
                    break;
                case TOK_RWORD_ENUM:
                    pmi.sendRword("enum");
                    break;
                case TOK_RWORD_TRAIT:
                    pmi.sendRword("trait");
                    break;
                case TOK_RWORD_FN:
                    pmi.sendRword("fn");
                    break;
                case TOK_RWORD_USE:
                    pmi.sendRword("use");
                    break;
                case TOK_RWORD_IMPL:
                    pmi.sendRword("impl");
                    break;
                case TOK_RWORD_TYPE:
                    pmi.sendRword("type");
                    break;
                case TOK_RWORD_WHERE:
                    pmi.sendRword("where");
                    break;
                case TOK_RWORD_AS:
                    pmi.sendRword("as");
                    break;
                case TOK_RWORD_LET:
                    pmi.sendRword("let");
                    break;
                case TOK_RWORD_MATCH:
                    pmi.sendRword("match");
                    break;
                case TOK_RWORD_IF:
                    pmi.sendRword("if");
                    break;
                case TOK_RWORD_ELSE:
                    pmi.sendRword("else");
                    break;
                case TOK_RWORD_LOOP:
                    pmi.sendRword("loop");
                    break;
                case TOK_RWORD_WHILE:
                    pmi.sendRword("while");
                    break;
                case TOK_RWORD_FOR:
                    pmi.sendRword("for");
                    break;
                case TOK_RWORD_IN:
                    pmi.sendRword("in");
                    break;
                case TOK_RWORD_DO:
                    pmi.sendRword("do");
                    break;
                case TOK_RWORD_CONTINUE:
                    pmi.sendRword("continue");
                    break;
                case TOK_RWORD_BREAK:
                    pmi.sendRword("break");
                    break;
                case TOK_RWORD_RETURN:
                    pmi.sendRword("return");
                    break;
                case TOK_RWORD_YIELD:
                    pmi.sendRword("yeild");
                    break;
                case TOK_RWORD_BOX:
                    pmi.sendRword("box");
                    break;
                case TOK_RWORD_REF:
                    pmi.sendRword("ref");
                    break;
                case TOK_RWORD_FALSE:
                    pmi.sendRword("false");
                    break;
                case TOK_RWORD_TRUE:
                    pmi.sendRword("true");
                    break;
                case TOK_RWORD_SELF:
                    pmi.sendRword("self");
                    break;
                case TOK_RWORD_SUPER:
                    pmi.sendRword("super");
                    break;
                case TOK_RWORD_MOVE:
                    pmi.sendRword("move");
                    break;
                case TOK_RWORD_ABSTRACT:
                    pmi.sendRword("abstract");
                    break;
                case TOK_RWORD_FINAL:
                    pmi.sendRword("final");
                    break;
                case TOK_RWORD_OVERRIDE:
                    pmi.sendRword("override");
                    break;
                case TOK_RWORD_VIRTUAL:
                    pmi.sendRword("virtual");
                    break;
                case TOK_RWORD_TYPEOF:
                    pmi.sendRword("typeof");
                    break;
                case TOK_RWORD_BECOME:
                    pmi.sendRword("become");
                    break;
                case TOK_RWORD_UNSIZED:
                    pmi.sendRword("unsized");
                    break;
                case TOK_RWORD_MACRO:
                    pmi.sendRword("macro");
                    break;

                // 2018
                case TOK_RWORD_ASYNC:
                    pmi.sendRword("async");
                    break;
                case TOK_RWORD_AWAIT:
                    pmi.sendRword("await");
                    break;
                case TOK_RWORD_DYN:
                    pmi.sendRword("dyn");
                    break;
                case TOK_RWORD_TRY:
                    pmi.sendRword("try");
                    break;
            }
        }

        void visitTokentree(const ::TokenTree& tt) {
            if (tt.isToken()) {
                visitToken(tt.tok());
            } else {
                for (size_t i = 0; i < tt.size(); i++) {
                    visitTokentree(tt[i]);
                }
            }
        }

        void visitPattern(const ASTPattern& pat) {
            for (const auto& b : pat.bindings()) {
                if (b.isMutable) {
                    pmi.sendRword("mut");
                }
                switch (b.mType) {
                    case ASTPatternBinding::Type::MOVE:
                        break;
                    case ASTPatternBinding::Type::REF:
                        pmi.sendRword("ref");
                        break;
                    case ASTPatternBinding::Type::MUTREF:
                        pmi.sendRword("ref");
                        pmi.sendRword("mut");
                        break;
                }
                if (b.mName == "self") {
                    pmi.sendRword("self");
                    return;
                } else {
                    pmi.sendIdent(b.mName);
                }
                pmi.sendSymbol("@");
            }
            TU_MATCH_HDRA( (pat.data()), { )
            default:
                TODO(sp, "visit_pattern " << pat.data().tagStr() << " - " << pat);
                TU_ARMA(Any, e) {
                    pmi.sendRword("_");
                }
                TU_ARMA(MaybeBind, e) {
                    if (e.name == "self") {
                        pmi.sendRword("self");
                    } else {
                        pmi.sendIdent(e.name);
                    }
                }
                TU_ARMA(Tuple, e) {
                    pmi.sendSymbol("(");
                    visitTuplePattern(e);
                    pmi.sendSymbol(")");
                }
                TU_ARMA(Struct, e) {
                    this->visitPath(e.path);
                    pmi.sendSymbol("{");
                    for (const auto& spe : e.subPatterns) {
                        this->visitAttrs(spe.attrs);
                        pmi.sendIdent(spe.name);
                        pmi.sendSymbol(":");
                        this->visitPattern(spe.pat);
                        pmi.sendSymbol(",");
                    }
                    if (!e.isExhaustive) {
                        pmi.sendSymbol("...");
                    }
                    pmi.sendSymbol("}");
                }
            }
        }

        void visitTuplePattern(const ASTPattern::TuplePat& v) {
            for (const auto& p : v.start) {
                visitPattern(p);
                pmi.sendSymbol(",");
            }
            if (v.hasWildcard) {
                pmi.sendSymbol("..");
                pmi.sendSymbol(",");
                for (const auto& p : v.end) {
                    visitPattern(p);
                    pmi.sendSymbol(",");
                }
            }
        }

        void visitLifetime(const ASTLifetimeRef& x) {
            if (x.binding() == ASTLifetimeRef::BINDING_STATIC) {
                pmi.sendLifetime("static");
            } else if (x.binding() == ASTLifetimeRef::BINDING_INFER) {
                pmi.sendLifetime("_");
            } else if (x.binding() == ASTLifetimeRef::BINDING_UNSPECIFIED) {
                // Nothing
            } else {
                pmi.sendLifetime(x.name().name.c_str());
            }
        }

        void visitType(const ::TypeRef& ty) {
            // TODO: Correct handling of visit_type
            TU_MATCHA(
                (ty.mData),
                (te),
                (None, BUG(sp, ty);),
                (Any, pmi.sendRword("_");),
                (Bang, pmi.sendSymbol("!");),
                (Unit, pmi.sendSymbol("("); pmi.sendSymbol(")");),
                (Macro, visitPath(te.inv->path()); pmi.sendSymbol("!"); pmi.sendSymbol("("); visitTokentree(te.inv->inputTt()); pmi.sendSymbol(")");),
                (Primitive, TODO(sp, "proc_macro send primitive - " << ty);),
                (Function, ::std::stringstream ss; ss << ty << " "; DEBUG("STRING: " << ss.str());

                 parseString(ss.str());),
                (
                    Tuple, pmi.sendSymbol("("); for (const auto& st : te.innerTypes) {
                        this->visitType(st);
                        pmi.sendSymbol(",");
                    } pmi.sendSymbol(")");
                ),
                (Borrow, pmi.sendSymbol("&"); this->visitLifetime(te.lifetime); if (te.isMut) pmi.sendRword("mut"); pmi.sendSymbol("("); this->visitType(*te.inner); pmi.sendSymbol(")");),
                (Pointer, pmi.sendSymbol("*"); if (te.isMut) pmi.sendRword("mut"); else pmi.sendRword("const"); pmi.sendSymbol("("); this->visitType(*te.inner); pmi.sendSymbol(")");),
                (Array, pmi.sendSymbol("["); this->visitType(*te.inner); pmi.sendSymbol(";"); if (te.size) { this->visitNode(*te.size); } else { pmi.sendRword("_"); } pmi.sendSymbol("]");),
                (Slice, pmi.sendSymbol("["); this->visitType(*te.inner); pmi.sendSymbol("]");),
                (Generic,
                 // TODO: This may already be resolved?... Wait, how?
                 pmi.sendIdent(te.name.c_str());),
                (Path, this->visitPath(*te);),
                (
                    TraitObject, pmi.sendSymbol("("); pmi.sendRword("dyn"); bool needsPlus = false; for (const auto& t : te.traits) {
                        if (needsPlus) {
                            pmi.sendSymbol("+");
                        }
                        needsPlus = true;
                        this->visitHrbs(t.hrbs);
                        this->visitBoundConstness(t.constness);
                        this->visitPath(*t.path);
                    } for (const auto& lft : te.lifetimes) {
                        if (lft != ASTLifetimeRef()) {
                            if (needsPlus) {
                                pmi.sendSymbol("+");
                            }
                            needsPlus = true;
                            this->visitLifetime(lft);
                        }
                    } pmi.sendSymbol(")");
                ),
                (ErasedType, pmi.sendRword("impl"); bool needsPlus = false; for (const auto& t : te->traits) {
                    if (needsPlus) {
                        pmi.sendSymbol("+");
                    }
                    needsPlus = true;
                    this->visitHrbs(t.hrbs);
                    this->visitBoundConstness(t.constness);
                    this->visitPath(*t.path);
                } for (const auto& t : te->maybeTraits) {
                    if (needsPlus) {
                        pmi.sendSymbol("+");
                    }
                    needsPlus = true;
                    pmi.sendSymbol("?");
                    this->visitHrbs(t.hrbs);
                    this->visitPath(*t.path);
                } for (const auto& lft : te->lifetimes) {
                    if (needsPlus) {
                        pmi.sendSymbol("+");
                    }
                    needsPlus = true;
                    pmi.sendSymbol("+");
                    this->visitLifetime(lft);
                } if (te->use) { TODO(Span(), "`use`"); })
            )
        }

        void visitHrbs(const ASTHigherRankedBounds& hrbs) {
            if (!hrbs.empty()) {
                pmi.sendRword("for");
                pmi.sendSymbol("<");
                for (const auto& v : hrbs.mLifetimes) {
                    pmi.sendLifetime(v.name().name.c_str());
                    pmi.sendSymbol(",");
                }
                pmi.sendSymbol(">");
            }
        }

        void visitPathNode(const ASTPathNode& e, bool isExpr) {
            pmi.sendIdent(e.name().c_str());
            if (!e.args().isEmpty()) {
                if (e.args().isParen) {
                    auto& t = e.args().entries.at(0).as_Type();
                    this->visitType(t); // Should be a tuple
                    auto& rv = e.args().entries.at(1).as_AssociatedTyEqual();
                    pmi.sendSymbol("->");
                    this->visitType(rv.second);
                    return;
                }

                if (isExpr) {
                    pmi.sendSymbol("::");
                }
                pmi.sendSymbol("<");
                for (const auto& ent : e.args().entries) {
                    TU_MATCH_HDRA( (ent), {)
                    TU_ARMA(Null, _) {
                        }
                        TU_ARMA(Lifetime, l) {
                            pmi.sendLifetime(l.name().name.c_str());
                            pmi.sendSymbol(",");
                        }
                        TU_ARMA(Type, t) {
                            this->visitType(t);
                            pmi.sendSymbol(",");
                        }
                        TU_ARMA(Value, n) {
                            pmi.sendSymbol("{");
                            this->visitNode(*n);
                            pmi.sendSymbol("}");
                            pmi.sendSymbol(",");
                        }
                        TU_ARMA(AssociatedTyEqual, a) {
                            visitPathNode(a.first, false);
                            pmi.sendSymbol("=");
                            this->visitType(a.second);
                            pmi.sendSymbol(",");
                        }
                        TU_ARMA(AssociatedTyBound, a) {
                            visitPathNode(a.first, false);
                            pmi.sendSymbol(":");
                            for (const auto& p : a.second) {
                                if (&p != a.second.data()) {
                                    pmi.sendSymbol("+");
                                }
                                this->visitPath(p);
                            }
                            pmi.sendSymbol(",");
                        }
                    }
                }
                pmi.sendSymbol(">");
            }
        }

        void visitPath(const ASTPath& path, bool isExpr = false) {
            const ::std::vector<ASTPathNode>* nodes = nullptr;
            TU_MATCH_HDRA( (path.cls), {)
            TU_ARMA(Invalid, pe) {
                    BUG(sp, "Invalid path");
                }
                TU_ARMA(Local, pe) {
                    pmi.sendIdent(pe.name.c_str());
                }
                TU_ARMA(Relative, pe) {
                    // TODO: Send hygiene information
                    nodes = &pe.nodes;
                }
                TU_ARMA(Self, pe) {
                    pmi.sendRword("self");
                    if (!pe.nodes.empty()) {
                        pmi.sendSymbol("::");
                    }
                    nodes = &pe.nodes;
                }
                TU_ARMA(Super, pe) {
                    assert(pe.count > 0);
                    for (unsigned i = 0; i < pe.count; i++) {
                        if (i > 0) {
                            pmi.sendSymbol("::");
                        }
                        pmi.sendRword("super");
                    }
                    if (!pe.nodes.empty()) {
                        pmi.sendSymbol("::");
                    }
                    nodes = &pe.nodes;
                }
                TU_ARMA(Absolute, pe) {
                    if (pe.crate == "") {
                        pmi.sendRword("crate");
                    } else {
                        pmi.sendSymbol("::");
                        //m_pmi.send_string(pe.crate.c_str());
                        assert(pe.crate.c_str()[0] == '=');
                        pmi.sendIdent(pe.crate.c_str() + 1);
                    }
                    pmi.sendSymbol("::");
                    nodes = &pe.nodes;
                }
                TU_ARMA(UFCS, pe) {
                    pmi.sendSymbol("<");
                    this->visitType(*pe.type);
                    if (pe.trait) {
                        pmi.sendRword("as");
                        this->visitPath(*pe.trait);
                    }
                    pmi.sendSymbol(">");
                    pmi.sendSymbol("::");
                    nodes = &pe.nodes;
                }
            }
            bool first = true;
            for(const auto& e : *nodes)
            {
                if (!first) {
                    pmi.sendSymbol("::");
                }
                first = false;
                visitPathNode(e, isExpr);
            }
        }

        void visitParams(const ASTGenericParams& params) {
            if (!params.mParams.empty()) {
                bool isFirst = true;
                pmi.sendSymbol("<");
                for (const auto& param : params.mParams) {
                    if (!isFirst) {
                        pmi.sendSymbol(",");
                    }
                    TU_MATCH_HDRA( (param), {)
                    TU_ARMA(None, p) {
                            // Uh... oops?
                            BUG(sp, "Enountered GenericParam::None");
                        }
                        TU_ARMA(Lifetime, p) {
                            pmi.sendLifetime(p.name().name.c_str());
                            bool first = true;
                            for (size_t i = param.boundsStart; i < param.boundsEnd; i++) {
                                if (!params.bounds[i].is_None()) {
                                    if (first) {
                                        pmi.sendSymbol(":");
                                        first = false;
                                    } else {
                                        pmi.sendSymbol("+");
                                    }
                                }
                            TU_MATCH_HDRA((params.bounds[i]), {)
                            default:
                                BUG(sp, "");
                                    TU_ARMA(None, be) {
                                    }
                                    TU_ARMA(Lifetime, be) {
                                        pmi.sendLifetime(be.test.name().name.c_str());
                                    }
                            }
                            }
                        }
                        TU_ARMA(Type, p) {
                            this->visitAttrs(p.attrs());
                            pmi.sendIdent(p.name().c_str());
                            bool first = true;
                            for (size_t i = param.boundsStart; i < param.boundsEnd; i++) {
                                if (!params.bounds[i].is_None()) {
                                    if (first) {
                                        pmi.sendSymbol(":");
                                        first = false;
                                    } else {
                                        pmi.sendSymbol("+");
                                    }
                                }
                            TU_MATCH_HDRA((params.bounds[i]), {)
                            default:
                                BUG(sp, "Unhandled bound type - " << params.bounds[i]);
                                    TU_ARMA(None, be) {
                                    }
                                    TU_ARMA(TypeLifetime, be) {
                                        pmi.sendLifetime(be.bound.name().name.c_str());
                                    }
                                    TU_ARMA(IsTrait, be) {
                                        assert(be.outerHrbs.empty()); // Shouldn't be possible in this position
                                        if (!be.innerHrbs.empty()) {
                                            TODO(sp, "be.inner_hrbs");
                                        }
                                        visitBoundConstness(be.constness);
                                        visitPath(be.trait);
                                    }
                                    TU_ARMA(MaybeTrait, be) {
                                        pmi.sendSymbol("?");
                                        visitPath(be.trait);
                                    }
                            }
                            }
                            if (!p.getDefault().isWildcard()) {
                                pmi.sendSymbol("=");
                                this->visitType(p.getDefault());
                            }
                        }
                        TU_ARMA(Value, p) {
                            this->visitAttrs(p.attrs());
                            pmi.sendRword("const");
                            pmi.sendIdent(p.name().name.c_str());
                            pmi.sendSymbol(":");
                            visitType(p.type());
                            assert(param.boundsStart == param.boundsEnd);
                        }
                    }
                    isFirst = false;
                }
                pmi.sendSymbol(">");
            }
        }

        void visitHrb(const ASTHigherRankedBounds& hrb) {
            if (!hrb.empty()) {
                pmi.sendRword("for");
                pmi.sendSymbol("<");
                for (const auto& lft : hrb.mLifetimes) {
                    pmi.sendLifetime(lft.name().name.c_str());
                    pmi.sendSymbol(",");
                }
                pmi.sendSymbol(">");
            }
        }

        void visitBounds(const ASTGenericParams& params) {
            if (!params.bounds.empty()) {
                bool whereSent = false;

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

                    if (!whereSent) {
                        pmi.sendRword("where");
                        whereSent = true;
                    }
                    TU_MATCH_HDRA((e), {)
                    TU_ARMA(None, be)   continue;
                        TU_ARMA(Lifetime, be) {
                            pmi.sendLifetime(be.bound.name().name.c_str());
                            pmi.sendSymbol(":");
                            pmi.sendLifetime(be.test.name().name.c_str());
                        }
                        TU_ARMA(TypeLifetime, be) {
                            visitType(be.type);
                            pmi.sendSymbol(":");
                            pmi.sendLifetime(be.bound.name().name.c_str());
                        }
                        TU_ARMA(IsTrait, be) {
                            visitHrbs(be.outerHrbs);
                            visitType(be.type);
                            pmi.sendSymbol(":");
                            visitHrbs(be.innerHrbs);
                            visitBoundConstness(be.constness);
                            visitPath(be.trait);
                        }
                        TU_ARMA(MaybeTrait, be) {
                            visitType(be.type);
                            pmi.sendSymbol(":");
                            pmi.sendSymbol("?");
                            visitPath(be.trait);
                        }
                        TU_ARMA(NotTrait, be) {
                            visitType(be.type);
                            pmi.sendSymbol(":");
                            pmi.sendSymbol("!");
                            visitPath(be.trait);
                        }
                        TU_ARMA(Equality, be) {
                            visitType(be.type);
                            pmi.sendSymbol("=");
                            visitType(be.replacement);
                        }
                    }
                    pmi.sendSymbol(",");
                }
            }
        }

        void visitNode(const ASTExprNode& e) {
            DEBUG("NODE: " << e);
            // TODO: Dump to a string, then re-parse into a TT and then send that TT
            // - Avoids needing to repeat logic
            ::std::stringstream ss;
            DumpASTNode(ss, e);
            ss << " ";
            DEBUG("STRING: " << ss.str());

            //const_cast<::AST::ExprNode&>(e).visit(*this);
            parseString(ss.str());
        }

        void parseString(const ::std::string& s) {
            ::std::istringstream iss{s};
            Lexer l{iss, ASTEdition::Rust2021, {}};
            for (;;) {
                auto t = l.getToken();
                if (t == TOK_EOF) {
                    break;
                }
                // TODO: If this is an ident, then get the comment after it that specifies the hygine info
                visitToken(t);
            }
        }

        void visitNodes(const ASTExpr& e) {
            this->visitNode(e.node());
        }

        void visitTopAttrs(slice<const ASTAttribute>& attrs) {
            for (const auto& a : attrs) {
                this->visitAttr(a);
            }
        }

        void visitAttrs(const ASTAttributeList& attrs) {
            for (const auto& a : attrs.mItems) {
                this->visitAttr(a);
            }
        }

        void visitAttr(const ASTAttribute& a) {
            if (a.name() == "cfg_attr") {
                auto newAttrs = checkCfgAttr(a);
                for (const auto& na : newAttrs) {
                    this->visitAttr(na);
                }
            }
            if (this->skipDeriveAttrs && a.name().isTrivial() && (a.name().asTrivial() == "derive" || a.name().asTrivial() == "derive_const")) {
                DEBUG("Skip " << a << " (derive input)");
                return;
            }
            auto isLocal = (a.name().isTrivial() && pmi.attrIsUsed(a.name().asTrivial()));
            if (this->emitAllAttrs || isLocal) {
                if (isLocal) {
                    a.markInert();
                }
                DEBUG("Send " << a);
                pmi.sendSymbol("#");
                pmi.sendSymbol("[");
                this->visitMetaItem(a);
                pmi.sendSymbol("]");
            } else {
                DEBUG("Skip " << a << " (" << pmi.procMacroDesc.attributes << ")");
            }
        }

        void visitMetaItem(const ASTAttribute& i) {
            if (i.name().hasLeading) {
                pmi.sendSymbol("::");
            }
            for (const auto& e : i.name().elems) {
                if (&e != &i.name().elems.front()) {
                    pmi.sendSymbol("::");
                }
                pmi.sendIdent(e.c_str());
            }

            visitTokentree(i.data());
        }

        void visitVis(const ASTVisibility& vis) {
            switch (vis.ty()) {
                case ASTVisibility::Ty::Private:
                    break;
                case ASTVisibility::Ty::Pub:
                    pmi.sendRword("pub");
                    break;
                case ASTVisibility::Ty::Crate:
                    pmi.sendRword("crate");
                    break;
                case ASTVisibility::Ty::PubCrate:
                    pmi.sendRword("pub");
                    pmi.sendSymbol("(");
                    pmi.sendRword("crate");
                    pmi.sendSymbol(")");
                    break;
                case ASTVisibility::Ty::PubSuper:
                    pmi.sendRword("pub");
                    pmi.sendSymbol("(");
                    pmi.sendRword("super");
                    pmi.sendSymbol(")");
                    break;
                case ASTVisibility::Ty::PubSelf:
                    pmi.sendRword("pub");
                    pmi.sendSymbol("(");
                    pmi.sendRword("self");
                    pmi.sendSymbol(")");
                    break;
                case ASTVisibility::Ty::PubIn:
                    pmi.sendRword("pub");
                    pmi.sendSymbol("(");
                    pmi.sendRword("in");
                    visitPath(vis.inPath());
                    pmi.sendSymbol(")");
                    break;
            }
        }

        void visitStruct(const RcString& name, const ASTVisibility& vis, const ASTStruct& str) {
            this->visitVis(vis);
            pmi.sendRword("struct");
            pmi.sendIdent(name.c_str());
            this->visitParams(str.params());
            TU_MATCH_HDRA((str.mData), {)
            TU_ARMA(Unit, se) {
                    this->visitBounds(str.params());
                    pmi.sendSymbol(";");
                }
                TU_ARMA(Tuple, se) {
                    pmi.sendSymbol("(");
                    for (const auto& si : se.ents) {
                        this->visitAttrs(si.mAttrs);
                        this->visitVis(si.vis);
                        this->visitType(si.mType);
                        pmi.sendSymbol(",");
                    }
                    pmi.sendSymbol(")");
                    this->visitBounds(str.params());
                    pmi.sendSymbol(";");
                }
                TU_ARMA(Struct, se) {
                    this->visitBounds(str.params());
                    pmi.sendSymbol("{");

                    for (const auto& si : se.ents) {
                        this->visitAttrs(si.mAttrs);
                        this->visitVis(si.vis);
                        pmi.sendIdent(si.mName.c_str());
                        pmi.sendSymbol(":");
                        this->visitType(si.mType);
                        if (si.defaultValue) {
                            pmi.sendSymbol("=");
                            this->visitNodes(si.defaultValue);
                        }
                        pmi.sendSymbol(",");
                    }
                    pmi.sendSymbol("}");
                }
            }
        }

        void visitEnum(const RcString& name, const ASTVisibility& vis, const ASTEnum& enm) {
            this->visitVis(vis);

            pmi.sendRword("enum");
            pmi.sendIdent(name.c_str());
            this->visitParams(enm.params());
            this->visitBounds(enm.params());
            pmi.sendSymbol("{");
            for (const auto& v : enm.variants()) {
                this->visitAttrs(v.mAttrs);
                pmi.sendIdent(v.mName.c_str());
                TU_MATCH_HDRA( (v.mData), { )
                TU_ARMA(Unit, e) {
                    }
                    TU_ARMA(Tuple, e) {
                        pmi.sendSymbol("(");
                        for (const auto& f : e.mItems) {
                            this->visitAttrs(f.mAttrs);
                            this->visitType(f.mType);
                            pmi.sendSymbol(",");
                        }
                        pmi.sendSymbol(")");
                    }
                    TU_ARMA(Struct, e) {
                        pmi.sendSymbol("{");
                        for (const auto& f : e.fields) {
                            this->visitAttrs(f.mAttrs);
                            pmi.sendIdent(f.mName.c_str());
                            pmi.sendSymbol(":");
                            this->visitType(f.mType);
                            pmi.sendSymbol(",");
                        }
                        pmi.sendSymbol("}");
                    }
                }
                if( v.discriminantValue)
                {
                    pmi.sendSymbol("=");
                    this->visitNodes(v.discriminantValue);
                }
                pmi.sendSymbol(",");
            }
            pmi.sendSymbol("}");
        }

        void visitUnion(const RcString& name, const ASTVisibility& vis, const ASTUnion& unn) {
            TODO(sp, "visit_union");
        }

        void visitFunction(const RcString& name, const ASTVisibility& vis, const ASTFunction& fcn) {
            this->visitVis(vis);

            if (fcn.isUnsafe()) {
                pmi.sendRword("unsafe");
            }
            if (fcn.isConst()) {
                pmi.sendRword("const");
            }
            if (fcn.isAsync()) {
                pmi.sendRword("async");
            }
            if (fcn.abi() != ABI_RUST) {
                pmi.sendRword("extern");
                pmi.sendString(fcn.abi());
            }
            pmi.sendRword("fn");
            pmi.sendIdent(name.c_str());
            this->visitParams(fcn.params());
            pmi.sendSymbol("(");
            for (const auto& arg : fcn.args()) {
                this->visitAttrs(arg.attrs);
                this->visitPattern(arg.pat);
                pmi.sendSymbol(":");
                this->visitType(arg.ty);
                pmi.sendSymbol(",");
            }
            if (fcn.isVariadic()) {
                pmi.sendSymbol("...");
            }
            pmi.sendSymbol(")");
            //if( fcn.rettype() != TypeRef() ) {
            pmi.sendSymbol("->");
            this->visitType(fcn.rettype());
            //}
            this->visitBounds(fcn.params());
            // A trait method declaration has no body - send `;` rather than dereferencing an absent node.
            if (fcn.code().isValid()) {
                this->visitNodes(fcn.code());
            } else {
                pmi.sendSymbol(";");
            }
        }

        void visitStatic(const RcString& name, const ASTVisibility& vis, const ASTStatic& i) {
            this->visitVis(vis);
            switch (i.sClass()) {
                case ASTStatic::CONST:
                    pmi.sendRword("const");
                    break;
                case ASTStatic::MUT:
                    pmi.sendRword("static");
                    pmi.sendRword("mut");
                    break;
                case ASTStatic::STATIC:
                    pmi.sendRword("static");
                    break;
            }
            pmi.sendIdent(name.c_str());
            //this->visit_params(i.params());
            pmi.sendSymbol(":");
            this->visitType(i.type());

            if (i.value()) {
                pmi.sendSymbol("=");
                this->visitNode(i.value().node());
            }
            //this->visit_bounds(i.params());
            pmi.sendSymbol(";");
        }

        void visitUse(const RcString& /*name*/, const ASTVisibility& vis, const ASTUseItem& item) {
            this->visitVis(vis);
            pmi.sendRword("use");

            if (item.entries.size() == 1) {
                visitPath(item.entries[0].path);
                if (item.entries[0].name == "") {
                    pmi.sendSymbol("::");
                    pmi.sendSymbol("*");
                } else if (item.entries[0].name != item.entries[0].path.nodes().back().name()) {
                    pmi.sendRword("as");
                    pmi.sendIdent(item.entries[0].name.c_str());
                } else {
                }
            } else {
                TODO(sp, "Multiple items");
            }
            pmi.sendSymbol(";");
        }

        void visitImplHdr(const ASTImplDef& impl) {
            pmi.sendRword("impl");
            if (impl.isConst()) {
                pmi.sendRword("const");
            }
            visitParams(impl.params());

            if (impl.trait().ent.isValid()) {
                visitPath(impl.trait().ent);
                pmi.sendRword("for");
            }
            visitType(impl.type());
            visitBounds(impl.params());
        }

        /// Send a trait definition to the proc macro.
        void visitTrait(const RcString& name, const ASTVisibility& vis, const ASTTrait& trait) {
            this->visitVis(vis);
            if (trait.isUnsafe()) {
                pmi.sendRword("unsafe");
            }
            pmi.sendRword("trait");
            pmi.sendIdent(name.c_str());
            this->visitParams(trait.params());

            // Supertraits and trait-level lifetime bounds: `trait Foo: Bar + 'a`
            bool first = true;
            for (const auto& st : trait.supertraits()) {
                pmi.sendSymbol(first ? ":" : "+");
                first = false;
                this->visitHrbs(st.ent.hrbs);
                this->visitBoundConstness(st.ent.constness);
                this->visitPath(*st.ent.path);
            }
            for (const auto& lft : trait.lifetimes()) {
                pmi.sendSymbol(first ? ":" : "+");
                first = false;
                pmi.sendLifetime(lft.ent.name().name.c_str());
            }
            this->visitBounds(trait.params());

            pmi.sendSymbol("{");
            // Trait items inherit the trait's visibility; mrustc records them as `pub`, which the plugin's parser rejects. Send them unqualified.
            const auto itemVis = ASTVisibility::makeBarePrivate();
            for (const auto& i : trait.items()) {
                this->visitAttrs(i.attrs);
                TU_MATCH_HDRA((i.data), {)
                default:
                    TODO(i.span, "visit_trait item - " << i.data.tagStr());
                    break;
                    TU_ARMA(Function, e) {
                        this->visitFunction(i.name, itemVis, e);
                    }
                    TU_ARMA(Static, e) {
                        this->visitStatic(i.name, itemVis, e);
                    }
                    // An associated type. Bounds live in `m_self_bounds` encoded as `Self: ...`, not the shape needed here, so only the un-bounded form is emitted.
                    TU_ARMA(Type, e) {
                        if (!e.selfBounds.bounds.empty()) {
                            TODO(i.span, "visit_trait - associated type with bounds - " << i.name);
                        }
                        this->visitVis(itemVis);
                        pmi.sendRword("type");
                        pmi.sendIdent(i.name.c_str());
                        this->visitParams(e.mParams);
                        if (e.mType.isValid()) {
                            pmi.sendSymbol("=");
                            this->visitType(e.mType);
                        }
                        pmi.sendSymbol(";");
                    }
                }
            }
            pmi.sendSymbol("}");
        }

        void visitImpl(const ASTImpl& impl) {
            visitImplHdr(impl.def());
            pmi.sendSymbol("{");
            for (const auto& i : impl.items()) {
                const auto& sp = i.sp;
                const auto& item = *i.data;
                TU_MATCH_HDRA((item), {)
                default:
                    TODO(sp, "Item " << item.tagStr());
                    break;
                    TU_ARMA(Function, e) {
                        visitFunction(i.name.c_str(), i.vis, e);
                    }
                    TU_ARMA(Static, e) {
                        visitStatic(i.name.c_str(), i.vis, e);
                    }
                }
            }
            pmi.sendSymbol("}");
        }

        void visitItem(const RcString& name, const ASTVisibility& vis, const ASTItem& item) {
            TU_MATCH_HDRA((item), {)
            default:
                TODO(sp, "visit_item - " << item.tagStr());
                break;
                TU_ARMA(Impl, e) {
                    visitImpl(e);
                }
                TU_ARMA(Use, e) {
                    visitUse(name, vis, e);
                }
                // Types
                TU_ARMA(Struct, e) {
                    visitStruct(name, vis, e);
                }
                TU_ARMA(Enum, e) {
                    visitEnum(name, vis, e);
                }
                TU_ARMA(Union, e) {
                    visitUnion(name, vis, e);
                }
                TU_ARMA(Trait, e) {
                    visitTrait(name, vis, e);
                }

                // Values
                TU_ARMA(Function, e) {
                    visitFunction(name, vis, e);
                }
            }
        }
    };
}

::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, const TokenTree* attrInput, std::function<void(Visitor& v)> cb) {
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
                v.visitTokentree((*attrInput)[i]);
            }
        }
        pmi.sendDone();
    }
    // 2. Feed item as a token stream.
    Visitor v(sp, pmi);
    cb(v);
    pmi.sendDone();
    // 3. Return boxed invocation instance
    return box$(pmi);
}

// --- Derive inputs
::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& itemName, const ASTStruct& i) {
    return ProcMacroInvoke(sp, crate, macPath, nullptr, [&](Visitor& v) {
        DEBUG("derive on struct");
        v.skipDeriveAttrs = true;
        v.visitTopAttrs(attrs);
        v.visitStruct(itemName, vis, i);
    });
}

::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& itemName, const ASTEnum& i) {
    return ProcMacroInvoke(sp, crate, macPath, nullptr, [&](Visitor& v) {
        DEBUG("derive on enum");
        v.skipDeriveAttrs = true;
        v.visitTopAttrs(attrs);
        v.visitEnum(itemName, vis, i);
    });
}

::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& itemName, const ASTUnion& i) {
    return ProcMacroInvoke(sp, crate, macPath, nullptr, [&](Visitor& v) {
        DEBUG("derive on union");
        v.skipDeriveAttrs = true;
        v.visitTopAttrs(attrs);
        v.visitUnion(itemName, vis, i);
    });
}

// --- attribute
::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, const TokenTree& tt, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& itemName, const ASTItem& i) {
    return ProcMacroInvoke(sp, crate, macPath, &tt, [&](Visitor& v) {
        v.emitAllAttrs = true;
        v.visitTopAttrs(attrs);
        v.visitItem(itemName, vis, i);
    });
}

// -- function-like input
::std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const ASTCrate& crate, const ::std::vector<RcString>& macPath, const TokenTree& tt) {
    return ProcMacroInvoke(sp, crate, macPath, nullptr, [&](Visitor& v) {
        v.visitTokentree(tt);
    });
}

ProcMacroInv::ProcMacroInv(const Span& sp, ASTEdition edition, const char* executable, const HIRProcMacro& procMacroDesc)
    : TokenStream(ParseState())
    , parentSpan(sp)
    , thisSpan(Span(parentSpan, procMacroDesc.path.crateName(), procMacroDesc.name))
    , procMacroDesc(procMacroDesc)
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
    int stdinPipes[2];
    if (pipe(stdinPipes) != 0) {
        BUG(sp, "Unable to create stdin pipe pair for proc macro, " << strerror(errno));
    }
    this->handles.childStdin = stdinPipes[1]; // Write end
    int stdoutPipes[2];
    if (pipe(stdoutPipes) != 0) {
        BUG(sp, "Unable to create stdout pipe pair for proc macro, " << strerror(errno));
    }
    this->handles.childStdout = stdoutPipes[0]; // Read end

    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_adddup2(&file_actions, stdinPipes[0], 0);
    posix_spawn_file_actions_adddup2(&file_actions, stdoutPipes[1], 1);
    posix_spawn_file_actions_addclose(&file_actions, stdinPipes[0]);
    posix_spawn_file_actions_addclose(&file_actions, stdinPipes[1]);
    posix_spawn_file_actions_addclose(&file_actions, stdoutPipes[0]);
    posix_spawn_file_actions_addclose(&file_actions, stdoutPipes[1]);

    char* argv[3] = {const_cast<char*>(executable), const_cast<char*>(procMacroDesc.name.c_str()), nullptr};
    DEBUG(argv[0] << " " << argv[1]);
    //char*   envp[] = { nullptr };
    int rv = posix_spawn(&this->handles.childPid, executable, &file_actions, nullptr, argv, environ);
    if (rv != 0) {
        BUG(sp, "Error in posix_spawn - " << rv << " - can't start `" << executable << "`");
    }

    posix_spawn_file_actions_destroy(&file_actions);
    // Close the ends we don't care about.
    close(stdinPipes[0]);
    close(stdoutPipes[1]);

    // Invocation span is #1 (#0 is always empty/undefined)
    this->sendSpanDef(1, sp);
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

ProcMacroInv::~ProcMacroInv() {
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

void ProcMacroInv::sendU8(uint8_t v) {
    this->sendBytesRaw(&v, 1);
}

void ProcMacroInv::sendBytes(const void* val, size_t size) {
    this->sendV128u(static_cast<uint64_t>(size));
    this->sendBytesRaw(val, size);
}

void ProcMacroInv::sendBytesRaw(const void* val, size_t size) {
    if (dumpFileOut.is_open()) {
        dumpFileOut.write(reinterpret_cast<const char*>(val), size);
    }
    if (write(this->handles.childStdin, val, size) != static_cast<ssize_t>(size)) {
        BUG(parentSpan, "Error writing to child, " << strerror(errno));
    }
}

void ProcMacroInv::sendV128u(uint64_t val) {
    while (val >= 128) {
        this->sendU8(static_cast<uint8_t>(val & 0x7F) | 0x80);
        val >>= 7;
    }
    this->sendU8(static_cast<uint8_t>(val & 0x7F));
}

void ProcMacroInv::sendV128u(U128 val) {
    while (val >= U128(128)) {
        this->sendU8(static_cast<uint8_t>(val.truncateU64() & 0x7F) | 0x80);
        val >>= 7;
    }
    this->sendU8(static_cast<uint8_t>(val.truncateU64() & 0x7F));
}

uint8_t ProcMacroInv::recvU8() {
    uint8_t v;
    this->recvBytesRaw(&v, 1);
    return v;
}

::std::string ProcMacroInv::recvBytes() {
    auto len = this->recvV128u();
    ASSERT_BUG(this->parentSpan, len < SIZE_MAX, "Oversized string from child process");
    ::std::string val;
    val.resize(len);

    recvBytesRaw(&val[0], len);

    return val;
}

void ProcMacroInv::recvBytesRaw(void* outVoid, size_t len) {
    uint8_t* val = reinterpret_cast<uint8_t*>(outVoid);
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
        dumpFileRes.write(reinterpret_cast<const char*>(outVoid), len);
        dumpFileRes.flush();
    }
}

uint64_t ProcMacroInv::recvV128u() {
    uint64_t v = 0;
    unsigned ofs = 0;
    for (;;) {
        auto b = recvU8();
        v |= static_cast<uint64_t>(b & 0x7F) << ofs;
        if ((b & 0x80) == 0) {
            break;
        }
        ofs += 7;
    }
    return v;
}

U128 ProcMacroInv::recvV128uU128() {
    U128 v(0);
    unsigned ofs = 0;
    for (;;) {
        auto b = recvU8();
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
    uint8_t v = this->recvU8();

    switch (static_cast<TokenClass>(v)) {
        case TokenClass::EndOfStream:
            TODO(this->parentSpan, "EndOfStream");
        case TokenClass::SpanRef:
            TODO(this->parentSpan, "SpanDef");
        case TokenClass::SpanDef:
            TODO(this->parentSpan, "SpanDef");
            break;
        case TokenClass::Symbol: {
            auto val = this->recvBytes();
            if (val == "") {
                eofHit = true;
                return Token(TOK_EOF);
            }
            auto t = LexFindOperator(val);
            ASSERT_BUG(this->parentSpan, t != TOK_NULL, "Unknown symbol from child process - '" << val << "'");
            return t;
        }
        case TokenClass::Ident: {
            auto val = this->recvBytes();
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
            auto val = this->recvBytes();
            return Token(TOK_LIFETIME, RcString::newInterned(val));
        }
        case TokenClass::String: {
            auto val = this->recvBytes();
            return Token(TOK_STRING, mv$(val), this->getHygiene());
        }
        case TokenClass::ByteString: {
            auto val = this->recvBytes();
            return Token(TOK_BYTESTRING, mv$(val), this->getHygiene());
        }
        case TokenClass::CharLit: {
            auto val = this->recvV128u();
            return Token(U128(val), CORETYPE_CHAR);
        }
        case TokenClass::UnsignedInt: {
            ::eCoreType ty;
            switch (this->recvU8()) {
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
            auto val = this->recvV128uU128();
            return Token(val, ty);
        }
        case TokenClass::SignedInt: {
            ::eCoreType ty;
            switch (this->recvU8()) {
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
            auto val = this->recvV128uU128();
            if (val.truncateU64() & 1) {
                val = ~(val >> 1) + 1; // Negative (Is this even possible?)
                TODO(this->parentSpan, "Negative literal from proc macro, what?");
            } else {
                val = (val >> 1);
            }
            return Token(val, ty);
        }
        case TokenClass::Float: {
            ::eCoreType ty;
            switch (this->recvU8()) {
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
            this->recvBytesRaw(&val, sizeof(val));
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
