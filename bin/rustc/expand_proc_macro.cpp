#include "expand_proc_macro.h"

#include "common.h"
#include "synext.h"
#include "ast_ast.h"
#include "hir_hir.h"
#include "ast_dump.h"
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "parse_lex.h"
#include "expand_cfg.h"
#include "wire_board.h"
#include "main_bindings.h"
#include "parse_ttstream.h"

#include <std/str/view.h>
#include <std/lib/vector.h>

#include <spawn.h>
#include <unistd.h>
#include <sys/wait.h>
#include <unordered_set>

using namespace stl;

#if defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__) || defined(__DragonFly__) || defined(__APPLE__)
extern char** environ;
#endif

#define NEWNODE(ty, ...) ASTExprNodeP(new ASTExprNode##ty(__VA_ARGS__))

namespace {
    enum class TokenClass {
        EndOfStream = 0,
        Symbol = 1,
        Ident = 2,
        Lifetime = 3,
        String = 4,
        ByteString = 5,
        CharLit = 6,
        UnsignedInt = 7,
        SignedInt = 8,
        Float = 9,
        SpanRef = 10,
        SpanDef = 11,
        RawLiteral = 12,
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

    struct DecoratorProcMacroDerive: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorProcMacroAttribute: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct DecoratorProcMacro: public ExpandDecorator {
        AttrStage stage() const override;

        void handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const override;
    };

    struct ProcMacroInv: public TokenStream {
        Span parentSpan;
        Span thisSpan;
        const HIRProcMacro& procMacroDesc;
        ASTEdition edition;
        std::ofstream dumpFileOut;
        std::ofstream dumpFileRes;

        std::unordered_map<const SpanInner*, size_t> knownSpans;
        std::unordered_set<size_t> sentSpans;
        size_t nextSpanIndex = 2;

        struct Handles {
            Handles();

            Handles(Handles&&);
            Handles(const Handles&) = delete;
            Handles& operator=(Handles&&) = delete;
            Handles& operator=(const Handles&) = delete;
            pid_t childPid = 0;
            int childStdin = -1;
            int childStdout = -1;
        } handles;

        bool eofHit = false;
        Vector<u8> pendingSymbols;
        size_t pendingSymbolOffset = 0;

        ProcMacroInv(u32& id, const Span& sp, ASTEdition edition, const char* executable, const HIRProcMacro& procMacroDesc);
        ProcMacroInv(const ProcMacroInv&) = delete;
        ProcMacroInv(ProcMacroInv&&) = default;
        ProcMacroInv& operator=(const ProcMacroInv&) = delete;
        ProcMacroInv& operator=(ProcMacroInv&&) = delete;
        ~ProcMacroInv();

        bool checkGood();

        void sendDone();

        void sendSymbol(const char* val);

        void sendRword(const char* val);

        void sendIdent(const char* val);

        void sendIdent(const Ident& val);

        void sendLifetime(const char* val);

        void sendString(const std::string& s);

        void sendRawLiteral(const std::string& s);

        void sendBytestring(const std::string& s);

        void sendChar(u32 ch);

        void sendInt(eCoreType ct, U128 v);

        void sendFloat(eCoreType ct, FloatValue v);

        void sendSpanDef(size_t index, const Span& sp);

        bool attrIsUsed(const RcString& n) const;

        virtual Position getPosition() const override;
        virtual Token realGetToken() override;

        virtual ASTEdition realGetEdition() const override;

        virtual Ident::Hygiene realGetHygiene() const override;

        Token realGetToken_();
        Token takePendingSymbol();
        void sendU8(u8 v);
        void sendBytes(const void* val, size_t size);
        void sendBytesRaw(const void* val, size_t size);
        void sendV128u(u64 val);
        void sendV128u(U128 val);

        u8 recvU8();
        std::string recvBytes();
        void recvBytesRaw(void* outVoid, size_t len);
        u64 recvV128u();
        U128 recvV128uU128();
    };

    struct ProcMacroVisitor {
        const WireBoard& wb;
        const Span& sp;
        const Settings& settings;
        ProcMacroInv& pmi;
        bool emitAllAttrs;
        bool skipDeriveAttrs = false;

        ProcMacroVisitor(const WireBoard& wb, const Span& sp, const Settings& settings, ProcMacroInv& pmi);

        void visitBoundConstness(ASTBoundConstness constness);

        void visitToken(const ::Token& tok);

        void visitTokentree(const ::TokenTree& tt);

        void visitPattern(const ASTPattern& pat);

        void visitTuplePattern(const ASTPattern::TuplePat& v);

        void visitLifetime(const ASTLifetimeRef& x);

        void visitTypeAsText(const ASTType* ty);

        void visitType(::ASTType* ty);

        void visitHrbs(const ASTHigherRankedBounds& hrbs);

        void visitPathNode(const ASTPathNode& e, bool isExpr);

        void visitPath(const ASTPath& path, bool isExpr = false);

        void visitParams(const ASTGenericParams& params);

        void visitHrb(const ASTHigherRankedBounds& hrb);

        void visitBounds(const ASTGenericParams& params);

        void visitNode(const ASTExprNode& e);

        void parseString(const std::string& s);

        void visitNodes(const ASTExpr& e);

        void visitTopAttrs(slice<const ASTAttribute>& attrs);

        void visitAttrs(const ASTAttributeList& attrs);

        void visitAttr(const ASTAttribute& a);

        void visitMetaItem(const ASTAttribute& i);

        void visitVis(const ASTVisibility& vis);

        void visitStruct(const RcString& name, const ASTVisibility& vis, const ASTStruct& str);

        void visitEnum(const RcString& name, const ASTVisibility& vis, const ASTEnum& enm);

        void visitUnion(const RcString& name, const ASTVisibility& vis, const ASTUnion& unn);

        void visitFunction(const RcString& name, const ASTVisibility& vis, const ASTFunction& fcn);

        void visitStatic(const RcString& name, const ASTVisibility& vis, const ASTStatic& i);

        void visitUse(const RcString& /*name*/, const ASTVisibility& vis, const ASTUseItem& item);

        void visitImplHdr(const ASTImplDef& impl);

        void visitTrait(const RcString& name, const ASTVisibility& vis, const ASTTrait& trait);

        void visitImpl(const ASTImpl& impl);

        void visitItem(const RcString& name, const ASTVisibility& vis, const ASTItem& item);
    };

    ProcMacroInv ProcMacroInvokeInt(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath) {
        const auto& crateName = macPath.front();
        ASSERT_BUG(sp, crate.externCrates.count(crateName), "Crate not loaded for macro: [" << macPath << "]");
        const auto& extCrate = crate.externCrates.at(crateName);
        // TODO: Ensure that this macro is in the listed crate.
        const HIRProcMacro* pmp = nullptr;
        for (const auto& mi : extCrate.hir->rootModule.macroItems) {
            if (!mi.second->ent.is_ProcMacro()) {
                continue;
            }
            const auto& pm = mi.second->ent.as_ProcMacro();
            bool good = true;
            for (size_t i = 0; i < std::min(macPath.size() - 1, pm.path.components().size()); i++) {
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

        const auto* procMacroExeName = extCrate.procMacroFilename != "" ? extCrate.procMacroFilename.c_str() : extCrate.filename.c_str();

        auto rv = ProcMacroInv(wb.id, sp, extCrate.hir->edition, procMacroExeName, *pmp);
        rv.parseState().crate = &crate;
        rv.parseState().wb = &wb;

        return rv;
    }

    template <typename F>
    std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, const TokenTree* attrInput, F cb) {
        auto pmi = ProcMacroInvokeInt(sp, wb, crate, macPath);
        if (!pmi.checkGood()) {
            return std::unique_ptr<TokenStream>();
        }
        if (attrInput) {
            // TODO: Assert that this is a `#[proc_macro_attribute]` macro
            if (attrInput->size() != 0) {
                ASSERT_BUG(sp, attrInput->size() >= 2, "");
                ASSERT_BUG(sp, (*attrInput)[0].tok() == TOK_PAREN_OPEN || (*attrInput)[0].tok() == TOK_SQUARE_OPEN, "");
                ProcMacroVisitor v(wb, sp, *wb.settings, pmi);
                for (size_t i = 1; i < attrInput->size() - 1; i++) {
                    v.visitTokentree((*attrInput)[i]);
                }
            }
            pmi.sendDone();
        }
        ProcMacroVisitor v(wb, sp, *wb.settings, pmi);
        cb(v);
        pmi.sendDone();
        return box$(pmi);
    }
}

void RegisterProcMacroBuiltins(ExpandRegistry& registry) {
    registry.addDecorator<DecoratorProcMacroDerive>("proc_macro_derive");
    registry.addDecorator<DecoratorProcMacroAttribute>("proc_macro_attribute");
    registry.addDecorator<DecoratorProcMacro>("proc_macro");
}

void ExpandProcMacroHarness(const WireBoard& wb, ASTCrate& crate) {
    auto pmCrateName = RcString::newInterned("proc_macro");
    wb.settings->implicitCrates.insert(std::make_pair(pmCrateName, const_cast<ASTCrate&>(crate).loadExternCrate(*wb.settings, Span(), pmCrateName)));

    auto mainFn = ASTFunction{Span(), mkType(*crate.pool, ASTTypeTags::Unit(), Span()), {}};
    {
        auto callNode = NEWNODE(CallPath, ASTPath(crate.extCratenameProcmacro, {ASTPathNode("main")}), ::makeVec1(NEWNODE(UniOp, ASTExprNodeUniOp::REF, NEWNODE(NamedValue, ASTPath("", {ASTPathNode("proc_macro#"), ASTPathNode("MACROS")})))));
        mainFn.setCode(mv$(callNode));
    }

    std::vector<ASTExprNodeP> testNodes;

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
        descVals.push_back({{}, "name", NEWNODE(String, desc.name.c_str())});
        descVals.push_back({{}, "handler", NEWNODE(CallPath, ASTPath(crate.extCratenameProcmacro, {ASTPathNode("MacroType"), ASTPathNode(typeName)}), ::makeVec1(NEWNODE(NamedValue, ASTPath(desc.path))))});

        testNodes.push_back(NEWNODE(StructLiteral, ASTPath(crate.extCratenameProcmacro, {ASTPathNode("MacroDesc")}), nullptr, mv$(descVals)));
    }
    auto* testsArray = new ASTExprNodeArray(mv$(testNodes));

    size_t testCount = testsArray->values.size();
    auto testsList = ASTStatic{ASTStatic::Class::STATIC, mkType(*crate.pool, ASTTypeTags::SizedArray(), Span(), mkType(*crate.pool, Span(), ASTPath(crate.extCratenameProcmacro, {ASTPathNode("MacroDesc")})), std::shared_ptr<ASTExprNode>(new ASTExprNodeInteger(U128(testCount), CORETYPE_UINT))), ASTExpr(mv$(testsArray))};

    auto newmod = ASTModule{ASTAbsolutePath("", {"proc_macro#"})};
    // - TODO: These need to be loaded too.

    auto visPrivate = ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, newmod.path());
    newmod.addExtCrate(Span(), visPrivate, crate.extCratenameProcmacro, "proc_macro", {});

    newmod.addItem(Span(), visPrivate, "main", mv$(mainFn), {});
    newmod.addItem(Span(), visPrivate, "MACROS", mv$(testsList), {});

    crate.rootModule_.addItem(Span(), visPrivate, "proc_macro#", mv$(newmod), {});
    crate.langItems["trustme-main"] = ASTAbsolutePath("", {"proc_macro#", "main"});
}

std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& itemName, const ASTStruct& i) {
    return ProcMacroInvoke(sp, wb, crate, macPath, nullptr, [&](ProcMacroVisitor& v) {
        v.skipDeriveAttrs = true;
        v.visitTopAttrs(attrs);
        v.visitStruct(itemName, vis, i);
    });
}

std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& itemName, const ASTEnum& i) {
    return ProcMacroInvoke(sp, wb, crate, macPath, nullptr, [&](ProcMacroVisitor& v) {
        v.skipDeriveAttrs = true;
        v.visitTopAttrs(attrs);
        v.visitEnum(itemName, vis, i);
    });
}

std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& itemName, const ASTUnion& i) {
    return ProcMacroInvoke(sp, wb, crate, macPath, nullptr, [&](ProcMacroVisitor& v) {
        v.skipDeriveAttrs = true;
        v.visitTopAttrs(attrs);
        v.visitUnion(itemName, vis, i);
    });
}

std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, const TokenTree& tt, slice<const ASTAttribute> attrs, const ASTVisibility& vis, const RcString& itemName, const ASTItem& i) {
    return ProcMacroInvoke(sp, wb, crate, macPath, &tt, [&](ProcMacroVisitor& v) {
        v.emitAllAttrs = true;
        v.visitTopAttrs(attrs);
        v.visitItem(itemName, vis, i);
    });
}

std::unique_ptr<TokenStream> ProcMacroInvoke(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const std::vector<RcString>& macPath, const TokenTree& tt) {
    return ProcMacroInvoke(sp, wb, crate, macPath, nullptr, [&](ProcMacroVisitor& v) {
        v.visitTokentree(tt);
    });
}

ProcMacroInv::ProcMacroInv(u32& id, const Span& sp, ASTEdition edition, const char* executable, const HIRProcMacro& procMacroDesc)
    : TokenStream(ParseState())
    , parentSpan(sp)
    , thisSpan(Span(parentSpan, procMacroDesc.path.crateName(), procMacroDesc.name))
    , procMacroDesc(procMacroDesc)
    , edition(edition)
{
    if (getenv("TRUSTME_DUMP_PROCMACRO") && getenv("TRUSTME_DUMP_PROCMACRO")[0]) {
        // TODO: Dump both input and output, AND (optionally) dump each invocation
        std::string namePrefix;
        namePrefix = FMT(getenv("TRUSTME_DUMP_PROCMACRO") << "-" << ++id);
        dumpFileOut.open(FMT(namePrefix << "-out.bin"), std::ios::out | std::ios::binary);
        dumpFileRes.open(FMT(namePrefix << "-res.bin"), std::ios::out | std::ios::binary);
    }
    int stdinPipes[2];
    if (pipe(stdinPipes) != 0) {
        BUG(sp, "Unable to create stdin pipe pair for proc macro, " << strerror(errno));
    }
    this->handles.childStdin = stdinPipes[1];
    int stdoutPipes[2];
    if (pipe(stdoutPipes) != 0) {
        BUG(sp, "Unable to create stdout pipe pair for proc macro, " << strerror(errno));
    }
    this->handles.childStdout = stdoutPipes[0];

    posix_spawn_file_actions_t file_actions;
    posix_spawn_file_actions_init(&file_actions);
    posix_spawn_file_actions_adddup2(&file_actions, stdinPipes[0], 0);
    posix_spawn_file_actions_adddup2(&file_actions, stdoutPipes[1], 1);
    posix_spawn_file_actions_addclose(&file_actions, stdinPipes[0]);
    posix_spawn_file_actions_addclose(&file_actions, stdinPipes[1]);
    posix_spawn_file_actions_addclose(&file_actions, stdoutPipes[0]);
    posix_spawn_file_actions_addclose(&file_actions, stdoutPipes[1]);

    char* argv[3] = {const_cast<char*>(executable), const_cast<char*>(procMacroDesc.name.c_str()), nullptr};
    int rv = posix_spawn(&this->handles.childPid, executable, &file_actions, nullptr, argv, environ);
    if (rv != 0) {
        BUG(sp, "Error in posix_spawn - " << rv << " - can't start `" << executable << "`");
    }

    posix_spawn_file_actions_destroy(&file_actions);
    close(stdinPipes[0]);
    close(stdoutPipes[1]);

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
}

ProcMacroInv::~ProcMacroInv() {
    if (this->handles.childPid != 0) {
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
        return false;
    }
    if (rv < 0) {
        return false;
    }
    if (v != 0) {
        return false;
    }
    return true;
}

void ProcMacroInv::sendU8(u8 v) {
    this->sendBytesRaw(&v, 1);
}

void ProcMacroInv::sendBytes(const void* val, size_t size) {
    this->sendV128u(static_cast<u64>(size));
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

void ProcMacroInv::sendV128u(u64 val) {
    while (val >= 128) {
        this->sendU8(static_cast<u8>(val & 0x7F) | 0x80);
        val >>= 7;
    }
    this->sendU8(static_cast<u8>(val & 0x7F));
}

void ProcMacroInv::sendV128u(U128 val) {
    while (val >= U128(128)) {
        this->sendU8(static_cast<u8>(val.truncateU64() & 0x7F) | 0x80);
        val >>= 7;
    }
    this->sendU8(static_cast<u8>(val.truncateU64() & 0x7F));
}

u8 ProcMacroInv::recvU8() {
    u8 v;
    this->recvBytesRaw(&v, 1);
    return v;
}

std::string ProcMacroInv::recvBytes() {
    auto len = this->recvV128u();
    ASSERT_BUG(this->parentSpan, len < SIZE_MAX, "Oversized string from child process");
    std::string val;
    val.resize(len);

    recvBytesRaw(&val[0], len);

    return val;
}

void ProcMacroInv::recvBytesRaw(void* outVoid, size_t len) {
    u8* val = reinterpret_cast<u8*>(outVoid);
    size_t ofs = 0, rem = len;
    while (rem > 0) {
        auto n = read(this->handles.childStdout, &val[ofs], rem);
        if (n == 0) {
            BUG(this->thisSpan, "Unexpected EOF while reading from child process");
        }
        if (n < 0) {
            BUG(this->parentSpan, "Error while reading from child process");
        }
        BUG_ASSERT(static_cast<size_t>(n) <= rem);
        ofs += n;
        rem -= n;
    }

    if (dumpFileRes.is_open()) {
        dumpFileRes.write(reinterpret_cast<const char*>(outVoid), len);
        dumpFileRes.flush();
    }
}

u64 ProcMacroInv::recvV128u() {
    u64 v = 0;
    unsigned ofs = 0;
    for (;;) {
        auto b = recvU8();
        v |= static_cast<u64>(b & 0x7F) << ofs;
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
    return Position(parentSpan);
}

Token ProcMacroInv::realGetToken() {
    auto rv = this->realGetToken_();
    return rv;
}

Token ProcMacroInv::realGetToken_() {
    if (pendingSymbolOffset != pendingSymbols.length()) {
        return this->takePendingSymbol();
    }
    if (eofHit) {
        return Token(TOK_EOF);
    }
    u8 v = this->recvU8();

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
            pendingSymbols.clear();
            pendingSymbols.append(reinterpret_cast<const u8*>(val.data()), val.size());
            pendingSymbolOffset = 0;
            return this->takePendingSymbol();
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
                val = ~(val >> 1) + 1;
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
        case TokenClass::RawLiteral: {
            auto text = this->recvBytes();
            std::istringstream input(text + " ");
            Lexer lexer(this->parseState().wb->id, this->typePool(), input, edition, this->parseState());
            auto token = lexer.getToken();
            ASSERT_BUG(this->parentSpan, token != TOK_EOF, "Empty raw literal from child process");
            ASSERT_BUG(this->parentSpan, lexer.getToken() == TOK_EOF, "Raw literal contains multiple tokens: `" << text << "`");
            token.setPos(this->getPosition());
            return token;
        }

            //    TODO(this->m_parent_span, "Handle ints/floats/fragments from child process");
    }
    BUG(this->parentSpan, "Invalid token class from child process - " << int(v));

    UNREACHABLE();
}

Token ProcMacroInv::takePendingSymbol() {
    const StringView remaining(pendingSymbols.begin() + pendingSymbolOffset, pendingSymbols.length() - pendingSymbolOffset);
    for (size_t len = remaining.length(); len != 0; --len) {
        auto token = LexFindOperator(remaining.prefix(len));
        if (token != TOK_NULL) {
            pendingSymbolOffset += len;
            return token;
        }
    }
    BUG(this->parentSpan, "Unknown symbol byte from child process - " << remaining[0]);
}

Ident::Hygiene ProcMacroInv::realGetHygiene() const {
    return Ident::Hygiene();
}

auto DecoratorProcMacroDerive::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto DecoratorProcMacroDerive::handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_None()) {
        return;
    }

    if (!i.is_Function()) {
        TODO(sp, "Error for proc_macro_derive on non-Function");
    }

    std::vector<std::string> attributes;
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

auto DecoratorProcMacroAttribute::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto DecoratorProcMacroAttribute::handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_None()) {
        return;
    }

    if (!i.is_Function()) {
        TODO(sp, "Error for #[proc_macro_attribute] on non-Function");
    }

    crate.procMacros.push_back(ASTProcMacroDef{ASTProcMacroTy::Attribute, path.nodes.back(), path, {}});
}

auto DecoratorProcMacro::stage() const -> AttrStage {
    return AttrStage::Post;
}

auto DecoratorProcMacro::handle(const Span& sp, const ASTAttribute& attr, const WireBoard& wb, ASTCrate& crate, const ASTAbsolutePath& path, ASTModule&, size_t, slice<const ASTAttribute> attrs, const ASTVisibility& vis, ASTItem& i) const -> void {
    if (i.is_None()) {
        return;
    }

    if (!i.is_Function()) {
        TODO(sp, "Error for #[proc_macro] on non-Function");
    }

    crate.procMacros.push_back(ASTProcMacroDef{ASTProcMacroTy::Function, path.nodes.back(), path, {}});
}

auto ProcMacroInv::sendDone() -> void {
    this->sendU8(static_cast<u8>(TokenClass::EndOfStream));
    dumpFileOut.flush();
}

auto ProcMacroInv::sendSymbol(const char* val) -> void {
    this->sendU8(static_cast<u8>(TokenClass::Symbol));
    this->sendBytes(val, std::strlen(val));
}

auto ProcMacroInv::sendRword(const char* val) -> void {
    this->sendU8(static_cast<u8>(TokenClass::Ident));
    this->sendBytes(val, std::strlen(val));
}

auto ProcMacroInv::sendIdent(const char* val) -> void {
    this->sendU8(static_cast<u8>(TokenClass::Ident));
    if (LexFindReservedWord(val, edition) != TOK_NULL) {
        auto size = std::strlen(val);
        this->sendV128u(2 + size);
        this->sendBytesRaw("r#", 2);
        this->sendBytesRaw(val, size);
    } else {
        this->sendBytes(val, std::strlen(val));
    }
}

auto ProcMacroInv::sendIdent(const Ident& val) -> void {
    sendIdent(val.name.c_str());
}

auto ProcMacroInv::sendLifetime(const char* val) -> void {
    this->sendU8(static_cast<u8>(TokenClass::Lifetime));
    this->sendBytes(val, std::strlen(val));
}

auto ProcMacroInv::sendString(const std::string& s) -> void {
    this->sendU8(static_cast<u8>(TokenClass::String));
    this->sendBytes(s.data(), s.size());
}

auto ProcMacroInv::sendRawLiteral(const std::string& s) -> void {
    this->sendU8(static_cast<u8>(TokenClass::RawLiteral));
    this->sendBytes(s.data(), s.size());
}

auto ProcMacroInv::sendBytestring(const std::string& s) -> void {
    this->sendU8(static_cast<u8>(TokenClass::ByteString));
    this->sendBytes(s.data(), s.size());
}

auto ProcMacroInv::sendChar(u32 ch) -> void {
    this->sendU8(static_cast<u8>(TokenClass::CharLit));
    this->sendV128u(ch);
}

auto ProcMacroInv::sendInt(eCoreType ct, U128 v) -> void {
    u8 size;
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
            this->sendU8(static_cast<u8>(TokenClass::UnsignedInt));
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
            this->sendU8(static_cast<u8>(TokenClass::SignedInt));
            this->sendU8(size);
            break;
        default:
            BUG(parentSpan, "Unknown integer type");
    }
    this->sendV128u(v);
}

auto ProcMacroInv::sendFloat(eCoreType ct, FloatValue v) -> void {
    this->sendU8(static_cast<u8>(TokenClass::Float));
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

auto ProcMacroInv::sendSpanDef(size_t index, const Span& sp) -> void {
    this->knownSpans[sp.get()] = index;
    this->sentSpans.insert(index);

    this->sendU8(static_cast<u8>(TokenClass::SpanDef));
    this->sendV128u(index);
    this->sendV128u(0); // TODO: Parent span
    if (const auto* spP = cast<const SpanInnerSource>(sp.get())) {
        this->sendBytes(spP->filename.c_str(), spP->filename.size());
        this->sendU8(1);
        this->sendV128u(spP->startLine);
        this->sendV128u(spP->endLine);
        this->sendV128u(spP->startOfs);
        this->sendV128u(spP->endOfs);
    } else {
        this->sendBytes("MACRO", 5); // TODO: better filename?
        this->sendU8(0);
        this->sendV128u(0);
        this->sendV128u(0);
        this->sendV128u(0);
        this->sendV128u(0);
    }
}

auto ProcMacroInv::attrIsUsed(const RcString& n) const -> bool {
    if (n == "repr") {
        return true;
    }
    return std::find(procMacroDesc.attributes.begin(), procMacroDesc.attributes.end(), n) != procMacroDesc.attributes.end();
}

auto ProcMacroInv::realGetEdition() const -> ASTEdition {
    return edition;
}

ProcMacroInv::Handles::Handles() {
}

ProcMacroVisitor::ProcMacroVisitor(const WireBoard& wb, const Span& sp, const Settings& settings, ProcMacroInv& pmi)
    : wb(wb)
    , sp(sp)
    , settings(settings)
    , pmi(pmi)
    , emitAllAttrs(true)
{
}

auto ProcMacroVisitor::visitBoundConstness(ASTBoundConstness constness) -> void {
    if (constness == ASTBoundConstness::Always) {
        pmi.sendRword("const");
    } else if (constness == ASTBoundConstness::Maybe) {
        pmi.sendSymbol("[");
        pmi.sendRword("const");
        pmi.sendSymbol("]");
    }
}

auto ProcMacroVisitor::visitToken(const ::Token& tok) -> void {
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
        case TOK_LITERAL_SUFFIXED:
            pmi.sendRawLiteral(tok.str());
            break;

        case TOK_HASH:
            pmi.sendSymbol("#");
            break;
        case TOK_UNDERSCORE:
            pmi.sendRword("_");
            break;

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

auto ProcMacroVisitor::visitTokentree(const ::TokenTree& tt) -> void {
    if (tt.isToken()) {
        visitToken(tt.tok());
    } else {
        for (size_t i = 0; i < tt.size(); i++) {
            visitTokentree(tt[i]);
        }
    }
}

auto ProcMacroVisitor::visitPattern(const ASTPattern& pat) -> void {
    for (const auto& b : pat.bindings()) {
        if (b.isMutable) {
            pmi.sendRword("mut");
        }
        switch (b.type) {
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
        if (b.name == "self") {
            pmi.sendRword("self");
            return;
        } else {
            pmi.sendIdent(b.name);
        }
        pmi.sendSymbol("@");
    }
    switch (pat.data().tag()) {
        default:
            TODO(sp, "visit_pattern " << pat.data().tagStr() << " - " << pat);
        case ASTPatternData::TAG_Any: {
            pmi.sendRword("_");
            break;
        }
        case ASTPatternData::TAG_MaybeBind: {
            auto& e = pat.data().as_MaybeBind();
            if (e.name == "self") {
                pmi.sendRword("self");
            } else {
                pmi.sendIdent(e.name);
            }
            break;
        }
        case ASTPatternData::TAG_Tuple: {
            auto& e = pat.data().as_Tuple();
            pmi.sendSymbol("(");
            visitTuplePattern(e);
            pmi.sendSymbol(")");
            break;
        }
        case ASTPatternData::TAG_Deref: {
            auto& e = pat.data().as_Deref();
            pmi.sendIdent(Ident({}, "deref"));
            pmi.sendSymbol("!");
            pmi.sendSymbol("(");
            visitPattern(*e.sub);
            pmi.sendSymbol(")");
            break;
        }
        case ASTPatternData::TAG_Struct: {
            auto& e = pat.data().as_Struct();
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
            break;
        }
    }
}

auto ProcMacroVisitor::visitTuplePattern(const ASTPattern::TuplePat& v) -> void {
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

auto ProcMacroVisitor::visitLifetime(const ASTLifetimeRef& x) -> void {
    if (x.binding() == ASTLifetimeRef::BINDING_STATIC) {
        pmi.sendLifetime("static");
    } else if (x.binding() == ASTLifetimeRef::BINDING_INFER) {
        pmi.sendLifetime("_");
    } else if (x.binding() == ASTLifetimeRef::BINDING_UNSPECIFIED) {
    } else {
        pmi.sendLifetime(x.name().name.c_str());
    }
}

auto ProcMacroVisitor::visitTypeAsText(const ASTType* ty) -> void {
    std::stringstream ss;
    ss << ty << " ";
    parseString(ss.str());
}

auto ProcMacroVisitor::visitType(::ASTType* ty) -> void {
    // TODO: Correct handling of visit_type
    switch (ty->data.tag()) {
        case TypeData::TAG_None: {
            BUG(sp, ty);
            break;
        }
        case TypeData::TAG_Any: {
            pmi.sendRword("_");
            break;
        }
        case TypeData::TAG_Bang: {
            pmi.sendSymbol("!");
            break;
        }
        case TypeData::TAG_Unit: {
            pmi.sendSymbol("(");
            pmi.sendSymbol(")");
            break;
        }
        case TypeData::TAG_Macro: {
            auto& te = ty->data.as_Macro();
            visitPath(te.inv->path());
            pmi.sendSymbol("!");
            pmi.sendSymbol("(");
            visitTokentree(te.inv->inputTt());
            pmi.sendSymbol(")");
            break;
        }
        case TypeData::TAG_Primitive: {
            TODO(sp, "proc_macro send primitive - " << ty);
            break;
        }
        case TypeData::TAG_Function: {
            visitTypeAsText(ty);
            break;
        }
        case TypeData::TAG_Tuple: {
            auto& te = ty->data.as_Tuple();
            pmi.sendSymbol("(");
            for (const auto& st : te.innerTypes) {
                this->visitType(st);
                pmi.sendSymbol(",");
            }
            pmi.sendSymbol(")");
            break;
        }
        case TypeData::TAG_Borrow: {
            auto& te = ty->data.as_Borrow();
            pmi.sendSymbol("&");
            this->visitLifetime(te.lifetime);
            if (te.isMut) {
                pmi.sendRword("mut");
            }
            pmi.sendSymbol("(");
            this->visitType(te.inner);
            pmi.sendSymbol(")");
            break;
        }
        case TypeData::TAG_Pointer: {
            auto& te = ty->data.as_Pointer();
            pmi.sendSymbol("*");
            if (te.isMut) {
                pmi.sendRword("mut");
            } else {
                pmi.sendRword("const");
            }
            pmi.sendSymbol("(");
            this->visitType(te.inner);
            pmi.sendSymbol(")");
            break;
        }
        case TypeData::TAG_Array: {
            auto& te = ty->data.as_Array();
            pmi.sendSymbol("[");
            this->visitType(te.inner);
            pmi.sendSymbol(";");
            if (te.size) {
                this->visitNode(*te.size);
            } else {
                pmi.sendRword("_");
            }
            pmi.sendSymbol("]");
            break;
        }
        case TypeData::TAG_Slice: {
            auto& te = ty->data.as_Slice();
            pmi.sendSymbol("[");
            this->visitType(te.inner);
            pmi.sendSymbol("]");
            break;
        }
        case TypeData::TAG_Pattern: {
            visitTypeAsText(ty);
            break;
        }
        case TypeData::TAG_Generic: {
            auto& te = ty->data.as_Generic();
            // TODO: This may already be resolved?... Wait, how?
            pmi.sendIdent(te.name.c_str());
            break;
        }
        case TypeData::TAG_Path: {
            auto& te = ty->data.as_Path();
            this->visitPath(*te);
            break;
        }
        case TypeData::TAG_TraitObject: {
            auto& te = ty->data.as_TraitObject();
            pmi.sendSymbol("(");
            pmi.sendRword("dyn");
            bool needsPlus = false;
            for (const auto& t : te.traits) {
                if (needsPlus) {
                    pmi.sendSymbol("+");
                }
                needsPlus = true;
                this->visitHrbs(t.hrbs);
                this->visitBoundConstness(t.constness);
                this->visitPath(*t.path);
            }
            for (const auto& lft : te.lifetimes) {
                if (lft != ASTLifetimeRef()) {
                    if (needsPlus) {
                        pmi.sendSymbol("+");
                    }
                    needsPlus = true;
                    this->visitLifetime(lft);
                }
            }
            pmi.sendSymbol(")");
            break;
        }
        case TypeData::TAG_ErasedType: {
            auto& te = ty->data.as_ErasedType();
            pmi.sendRword("impl");
            bool needsPlus = false;
            for (const auto& t : te->traits) {
                if (needsPlus) {
                    pmi.sendSymbol("+");
                }
                needsPlus = true;
                this->visitHrbs(t.hrbs);
                this->visitBoundConstness(t.constness);
                this->visitPath(*t.path);
            }
            for (const auto& t : te->maybeTraits) {
                if (needsPlus) {
                    pmi.sendSymbol("+");
                }
                needsPlus = true;
                pmi.sendSymbol("?");
                this->visitHrbs(t.hrbs);
                this->visitPath(*t.path);
            }
            for (const auto& lft : te->lifetimes) {
                if (needsPlus) {
                    pmi.sendSymbol("+");
                }
                needsPlus = true;
                pmi.sendSymbol("+");
                this->visitLifetime(lft);
            }
            if (te->use) {
                TODO(Span(), "`use`");
            }
            break;
        }
    }
}

auto ProcMacroVisitor::visitHrbs(const ASTHigherRankedBounds& hrbs) -> void {
    if (!hrbs.empty()) {
        pmi.sendRword("for");
        pmi.sendSymbol("<");
        for (const auto& v : hrbs.lifetimes) {
            pmi.sendLifetime(v.name().name.c_str());
            pmi.sendSymbol(",");
        }
        for (const auto& v : hrbs.types) {
            pmi.sendIdent(v.c_str());
            pmi.sendSymbol(",");
        }
        pmi.sendSymbol(">");
    }
}

auto ProcMacroVisitor::visitPathNode(const ASTPathNode& e, bool isExpr) -> void {
    pmi.sendIdent(e.name().c_str());
    if (!e.args().isEmpty()) {
        if (e.args().isRtn) {
            pmi.sendSymbol("(");
            pmi.sendSymbol("..");
            pmi.sendSymbol(")");
            return;
        }
        if (e.args().isParen) {
            auto& t = e.args().entries.at(0).as_Type();
            this->visitType(t);
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
            switch (ent.tag()) {
                case ASTPathParamEnt::TAG_Null: {
                    auto& _ = ent.as_Null();
                    break;
                }
                case ASTPathParamEnt::TAG_Lifetime: {
                    auto& l = ent.as_Lifetime();
                    pmi.sendLifetime(l.name().name.c_str());
                    pmi.sendSymbol(",");
                    break;
                }
                case ASTPathParamEnt::TAG_Type: {
                    auto& t = ent.as_Type();
                    this->visitType(t);
                    pmi.sendSymbol(",");
                    break;
                }
                case ASTPathParamEnt::TAG_Value: {
                    auto& n = ent.as_Value();
                    pmi.sendSymbol("{");
                    this->visitNode(*n);
                    pmi.sendSymbol("}");
                    pmi.sendSymbol(",");
                    break;
                }
                case ASTPathParamEnt::TAG_AssociatedTyEqual: {
                    auto& a = ent.as_AssociatedTyEqual();
                    visitPathNode(a.first, false);
                    pmi.sendSymbol("=");
                    this->visitType(a.second);
                    pmi.sendSymbol(",");
                    break;
                }
                case ASTPathParamEnt::TAG_AssociatedValueEqual: {
                    auto& a = ent.as_AssociatedValueEqual();
                    visitPathNode(a.first, false);
                    pmi.sendSymbol("=");
                    this->visitNode(*a.second);
                    pmi.sendSymbol(",");
                    break;
                }
                case ASTPathParamEnt::TAG_AssociatedTyBound: {
                    auto& a = ent.as_AssociatedTyBound();
                    visitPathNode(a.first, false);
                    pmi.sendSymbol(":");
                    for (const auto& p : a.second) {
                        if (&p != a.second.data()) {
                            pmi.sendSymbol("+");
                        }
                        this->visitHrbs(p.hrbs);
                        this->visitBoundConstness(p.constness);
                        this->visitPath(*p.path);
                    }
                    pmi.sendSymbol(",");
                    break;
                }
            }
        }
        pmi.sendSymbol(">");
    }
}

auto ProcMacroVisitor::visitPath(const ASTPath& path, bool isExpr) -> void {
    const std::vector<ASTPathNode>* nodes = nullptr;
    switch (path.cls.tag()) {
        case ASTPathClass::TAG_Invalid: {
            BUG(sp, "Invalid path");
            break;
        }
        case ASTPathClass::TAG_Local: {
            auto& pe = path.cls.as_Local();
            pmi.sendIdent(pe.name.c_str());
            break;
        }
        case ASTPathClass::TAG_Relative: {
            auto& pe = path.cls.as_Relative();
            // TODO: Send hygiene information
            nodes = &pe.nodes;
            break;
        }
        case ASTPathClass::TAG_Self: {
            auto& pe = path.cls.as_Self();
            pmi.sendRword("self");
            if (!pe.nodes.empty()) {
                pmi.sendSymbol("::");
            }
            nodes = &pe.nodes;
            break;
        }
        case ASTPathClass::TAG_Super: {
            auto& pe = path.cls.as_Super();
            BUG_ASSERT(pe.count > 0);
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
            break;
        }
        case ASTPathClass::TAG_Absolute: {
            auto& pe = path.cls.as_Absolute();
            if (pe.crate == "") {
                pmi.sendRword("crate");
            } else {
                pmi.sendSymbol("::");
                BUG_ASSERT(pe.crate.c_str()[0] == '=');
                pmi.sendIdent(pe.crate.c_str() + 1);
            }
            pmi.sendSymbol("::");
            nodes = &pe.nodes;
            break;
        }
        case ASTPathClass::TAG_UFCS: {
            auto& pe = path.cls.as_UFCS();
            pmi.sendSymbol("<");
            this->visitType(pe.type);
            if (pe.trait) {
                pmi.sendRword("as");
                this->visitPath(*pe.trait);
            }
            pmi.sendSymbol(">");
            pmi.sendSymbol("::");
            nodes = &pe.nodes;
            break;
        }
    }
    bool first = true;
    for (const auto& e : *nodes) {
        if (!first) {
            pmi.sendSymbol("::");
        }
        first = false;
        visitPathNode(e, isExpr);
    }
}

auto ProcMacroVisitor::visitParams(const ASTGenericParams& params) -> void {
    if (!params.params.empty()) {
        bool isFirst = true;
        pmi.sendSymbol("<");
        for (const auto& param : params.params) {
            if (!isFirst) {
                pmi.sendSymbol(",");
            }
            switch (param.tag()) {
                case GenericParam::TAG_None: {
                    BUG(sp, "Enountered GenericParam::None");
                    break;
                }
                case GenericParam::TAG_Lifetime: {
                    auto& p = param.as_Lifetime();
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
                        {
                            auto& tuMatch = params.bounds[i];
                            switch (tuMatch.tag()) {
                                default:
                                    BUG(sp, "");
                                case ASTGenericBound::TAG_None: {
                                    break;
                                }
                                case ASTGenericBound::TAG_Lifetime: {
                                    auto& be = tuMatch.as_Lifetime();
                                    pmi.sendLifetime(be.test.name().name.c_str());
                                    break;
                                }
                            }
                        }
                    }
                    break;
                }
                case GenericParam::TAG_Type: {
                    auto& p = param.as_Type();
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
                        {
                            auto& tuMatch = params.bounds[i];
                            switch (tuMatch.tag()) {
                                default:
                                    BUG(sp, "Unhandled bound type - " << params.bounds[i]);
                                case ASTGenericBound::TAG_None: {
                                    break;
                                }
                                case ASTGenericBound::TAG_TypeLifetime: {
                                    auto& be = tuMatch.as_TypeLifetime();
                                    pmi.sendLifetime(be.bound.name().name.c_str());
                                    break;
                                }
                                case ASTGenericBound::TAG_IsTrait: {
                                    auto& be = tuMatch.as_IsTrait();
                                    BUG_ASSERT(be.outerHrbs.empty());
                                    if (!be.innerHrbs.empty()) {
                                        TODO(sp, "be.inner_hrbs");
                                    }
                                    visitBoundConstness(be.constness);
                                    visitPath(be.trait);
                                    break;
                                }
                                case ASTGenericBound::TAG_MaybeTrait: {
                                    auto& be = tuMatch.as_MaybeTrait();
                                    pmi.sendSymbol("?");
                                    visitPath(be.trait);
                                    break;
                                }
                            }
                        }
                    }
                    if (!p.getDefault()->isWildcard()) {
                        pmi.sendSymbol("=");
                        this->visitType(p.getDefault());
                    }
                    break;
                }
                case GenericParam::TAG_Value: {
                    auto& p = param.as_Value();
                    this->visitAttrs(p.attrs());
                    pmi.sendRword("const");
                    pmi.sendIdent(p.name().name.c_str());
                    pmi.sendSymbol(":");
                    visitType(p.type());
                    BUG_ASSERT(param.boundsStart == param.boundsEnd);
                    break;
                }
            }
            isFirst = false;
        }
        pmi.sendSymbol(">");
    }
}

auto ProcMacroVisitor::visitHrb(const ASTHigherRankedBounds& hrb) -> void {
    if (!hrb.empty()) {
        pmi.sendRword("for");
        pmi.sendSymbol("<");
        for (const auto& lft : hrb.lifetimes) {
            pmi.sendLifetime(lft.name().name.c_str());
            pmi.sendSymbol(",");
        }
        for (const auto& type : hrb.types) {
            pmi.sendIdent(type.c_str());
            pmi.sendSymbol(",");
        }
        pmi.sendSymbol(">");
    }
}

auto ProcMacroVisitor::visitBounds(const ASTGenericParams& params) -> void {
    if (!params.bounds.empty()) {
        bool whereSent = false;

        for (const auto& e : params.bounds) {
            size_t i = &e - params.bounds.data();
            bool alreadyEmitted = false;
            for (const auto& p : params.params) {
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
            switch (e.tag()) {
                case ASTGenericBound::TAG_None: {
                    continue;
                }
                case ASTGenericBound::TAG_Lifetime: {
                    auto& be = e.as_Lifetime();
                    pmi.sendLifetime(be.bound.name().name.c_str());
                    pmi.sendSymbol(":");
                    pmi.sendLifetime(be.test.name().name.c_str());
                    break;
                }
                case ASTGenericBound::TAG_TypeLifetime: {
                    auto& be = e.as_TypeLifetime();
                    visitType(be.type);
                    pmi.sendSymbol(":");
                    pmi.sendLifetime(be.bound.name().name.c_str());
                    break;
                }
                case ASTGenericBound::TAG_IsTrait: {
                    auto& be = e.as_IsTrait();
                    visitHrbs(be.outerHrbs);
                    visitType(be.type);
                    pmi.sendSymbol(":");
                    visitHrbs(be.innerHrbs);
                    visitBoundConstness(be.constness);
                    visitPath(be.trait);
                    break;
                }
                case ASTGenericBound::TAG_MaybeTrait: {
                    auto& be = e.as_MaybeTrait();
                    visitType(be.type);
                    pmi.sendSymbol(":");
                    pmi.sendSymbol("?");
                    visitPath(be.trait);
                    break;
                }
                case ASTGenericBound::TAG_NotTrait: {
                    auto& be = e.as_NotTrait();
                    visitType(be.type);
                    pmi.sendSymbol(":");
                    pmi.sendSymbol("!");
                    visitPath(be.trait);
                    break;
                }
                case ASTGenericBound::TAG_Equality: {
                    auto& be = e.as_Equality();
                    visitType(be.type);
                    pmi.sendSymbol("=");
                    visitType(be.replacement);
                    break;
                }
            }
            pmi.sendSymbol(",");
        }
    }
}

auto ProcMacroVisitor::visitNode(const ASTExprNode& e) -> void {
    // TODO: Dump to a string, then re-parse into a TT and then send that TT

    std::stringstream ss;
    DumpASTNode(ss, e);
    ss << " ";

    parseString(ss.str());
}

auto ProcMacroVisitor::parseString(const std::string& s) -> void {
    std::istringstream iss{s};
    Lexer l{wb.id, *wb.pool, iss, ASTEdition::Rust2021, {}};
    for (;;) {
        auto t = l.getToken();
        if (t == TOK_EOF) {
            break;
        }
        // TODO: If this is an ident, then get the comment after it that specifies the hygine info
        visitToken(t);
    }
}

auto ProcMacroVisitor::visitNodes(const ASTExpr& e) -> void {
    this->visitNode(e.node());
}

auto ProcMacroVisitor::visitTopAttrs(slice<const ASTAttribute>& attrs) -> void {
    for (const auto& a : attrs) {
        this->visitAttr(a);
    }
}

auto ProcMacroVisitor::visitAttrs(const ASTAttributeList& attrs) -> void {
    for (const auto& a : attrs.items) {
        this->visitAttr(a);
    }
}

auto ProcMacroVisitor::visitAttr(const ASTAttribute& a) -> void {
    if (a.name() == "cfg_attr") {
        auto newAttrs = checkCfgAttr(settings, a);
        for (const auto& na : newAttrs) {
            this->visitAttr(na);
        }
    }
    if (this->skipDeriveAttrs && a.name().isTrivial() && (a.name().asTrivial() == "derive" || a.name().asTrivial() == "derive_const")) {
        return;
    }
    auto isLocal = (a.name().isTrivial() && pmi.attrIsUsed(a.name().asTrivial()));
    if (this->emitAllAttrs || isLocal) {
        if (isLocal) {
            a.markInert();
        }
        pmi.sendSymbol("#");
        pmi.sendSymbol("[");
        this->visitMetaItem(a);
        pmi.sendSymbol("]");
    }
}

auto ProcMacroVisitor::visitMetaItem(const ASTAttribute& i) -> void {
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

auto ProcMacroVisitor::visitVis(const ASTVisibility& vis) -> void {
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

auto ProcMacroVisitor::visitStruct(const RcString& name, const ASTVisibility& vis, const ASTStruct& str) -> void {
    this->visitVis(vis);
    pmi.sendRword("struct");
    pmi.sendIdent(name.c_str());
    this->visitParams(str.params());
    switch (str.data.tag()) {
        case ASTStructData::TAG_Unit: {
            this->visitBounds(str.params());
            pmi.sendSymbol(";");
            break;
        }
        case ASTStructData::TAG_Tuple: {
            auto& se = str.data.as_Tuple();
            pmi.sendSymbol("(");
            for (const auto& si : se.ents) {
                this->visitAttrs(si.attrs);
                this->visitVis(si.vis);
                this->visitType(si.type);
                pmi.sendSymbol(",");
            }
            pmi.sendSymbol(")");
            this->visitBounds(str.params());
            pmi.sendSymbol(";");
            break;
        }
        case ASTStructData::TAG_Struct: {
            auto& se = str.data.as_Struct();
            this->visitBounds(str.params());
            pmi.sendSymbol("{");

            for (const auto& si : se.ents) {
                this->visitAttrs(si.attrs);
                this->visitVis(si.vis);
                pmi.sendIdent(si.name.c_str());
                pmi.sendSymbol(":");
                this->visitType(si.type);
                if (si.defaultValue) {
                    pmi.sendSymbol("=");
                    this->visitNodes(si.defaultValue);
                }
                pmi.sendSymbol(",");
            }
            pmi.sendSymbol("}");
            break;
        }
    }
}

auto ProcMacroVisitor::visitEnum(const RcString& name, const ASTVisibility& vis, const ASTEnum& enm) -> void {
    this->visitVis(vis);

    pmi.sendRword("enum");
    pmi.sendIdent(name.c_str());
    this->visitParams(enm.params());
    this->visitBounds(enm.params());
    pmi.sendSymbol("{");
    for (const auto& v : enm.variants()) {
        this->visitAttrs(v.attrs);
        pmi.sendIdent(v.name.c_str());
        switch (v.data.tag()) {
            case ASTEnumVariantData::TAG_Unit: {
                break;
            }
            case ASTEnumVariantData::TAG_Tuple: {
                auto& e = v.data.as_Tuple();
                pmi.sendSymbol("(");
                for (const auto& f : e.items) {
                    this->visitAttrs(f.attrs);
                    this->visitType(f.type);
                    pmi.sendSymbol(",");
                }
                pmi.sendSymbol(")");
                break;
            }
            case ASTEnumVariantData::TAG_Struct: {
                auto& e = v.data.as_Struct();
                pmi.sendSymbol("{");
                for (const auto& f : e.fields) {
                    this->visitAttrs(f.attrs);
                    pmi.sendIdent(f.name.c_str());
                    pmi.sendSymbol(":");
                    this->visitType(f.type);
                    pmi.sendSymbol(",");
                }
                pmi.sendSymbol("}");
                break;
            }
        }
        if (v.discriminantValue) {
            pmi.sendSymbol("=");
            this->visitNodes(v.discriminantValue);
        }
        pmi.sendSymbol(",");
    }
    pmi.sendSymbol("}");
}

auto ProcMacroVisitor::visitUnion(const RcString& name, const ASTVisibility& vis, const ASTUnion& unn) -> void {
    TODO(sp, "visit_union");
}

auto ProcMacroVisitor::visitFunction(const RcString& name, const ASTVisibility& vis, const ASTFunction& fcn) -> void {
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
    for (size_t i = 0; i < fcn.args().size(); i++) {
        const auto& arg = fcn.args()[i];
        this->visitAttrs(arg.attrs);
        this->visitPattern(arg.pat);
        pmi.sendSymbol(":");
        if (fcn.hasNamedVariadic() && i + 1 == fcn.args().size()) {
            pmi.sendSymbol("...");
        } else {
            this->visitType(arg.ty);
        }
        pmi.sendSymbol(",");
    }
    if (fcn.isVariadic() && !fcn.hasNamedVariadic()) {
        pmi.sendSymbol("...");
    }
    pmi.sendSymbol(")");
    pmi.sendSymbol("->");
    this->visitType(fcn.rettype());
    this->visitBounds(fcn.params());
    if (fcn.code().isValid()) {
        this->visitNodes(fcn.code());
    } else {
        pmi.sendSymbol(";");
    }
}

auto ProcMacroVisitor::visitStatic(const RcString& name, const ASTVisibility& vis, const ASTStatic& i) -> void {
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
    this->visitParams(i.params());
    pmi.sendSymbol(":");
    this->visitType(i.type());

    if (i.value()) {
        pmi.sendSymbol("=");
        this->visitNode(i.value().node());
    }
    this->visitBounds(i.params());
    pmi.sendSymbol(";");
}

auto ProcMacroVisitor::visitUse(const RcString& /*name*/, const ASTVisibility& vis, const ASTUseItem& item) -> void {
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

auto ProcMacroVisitor::visitImplHdr(const ASTImplDef& impl) -> void {
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

auto ProcMacroVisitor::visitTrait(const RcString& name, const ASTVisibility& vis, const ASTTrait& trait) -> void {
    this->visitVis(vis);
    if (trait.isUnsafe()) {
        pmi.sendRword("unsafe");
    }
    pmi.sendRword("trait");
    pmi.sendIdent(name.c_str());
    this->visitParams(trait.params());

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
    const auto itemVis = ASTVisibility::makeBarePrivate();
    for (const auto& i : trait.items()) {
        this->visitAttrs(i.attrs);
        switch (i.data.tag()) {
            default:
                TODO(i.span, "visit_trait item - " << i.data.tagStr());
                break;
            case ASTItem::TAG_Function: {
                auto& e = i.data.as_Function();
                this->visitFunction(i.name, itemVis, e);
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = i.data.as_Static();
                this->visitStatic(i.name, itemVis, e);
                break;
            }
            case ASTItem::TAG_Type: {
                auto& e = i.data.as_Type();
                if (!e.selfBounds.bounds.empty()) {
                    TODO(i.span, "visit_trait - associated type with bounds - " << i.name);
                }
                this->visitVis(itemVis);
                pmi.sendRword("type");
                pmi.sendIdent(i.name.c_str());
                this->visitParams(e.params_);
                if (e.type_->isValid()) {
                    pmi.sendSymbol("=");
                    this->visitType(e.type_);
                }
                pmi.sendSymbol(";");
                break;
            }
        }
    }
    pmi.sendSymbol("}");
}

auto ProcMacroVisitor::visitImpl(const ASTImpl& impl) -> void {
    visitImplHdr(impl.def());
    pmi.sendSymbol("{");
    for (const auto& i : impl.items()) {
        const auto& sp = i.sp;
        const auto& item = *i.data;
        switch (item.tag()) {
            default:
                TODO(sp, "Item " << item.tagStr());
                break;
            case ASTItem::TAG_Function: {
                auto& e = item.as_Function();
                visitFunction(i.name.c_str(), i.vis, e);
                break;
            }
            case ASTItem::TAG_Static: {
                auto& e = item.as_Static();
                visitStatic(i.name.c_str(), i.vis, e);
                break;
            }
        }
    }
    pmi.sendSymbol("}");
}

auto ProcMacroVisitor::visitItem(const RcString& name, const ASTVisibility& vis, const ASTItem& item) -> void {
    switch (item.tag()) {
        default:
            TODO(sp, "visit_item - " << item.tagStr());
            break;
        case ASTItem::TAG_Impl: {
            auto& e = item.as_Impl();
            visitImpl(e);
            break;
        }
        case ASTItem::TAG_Use: {
            auto& e = item.as_Use();
            visitUse(name, vis, e);
            break;
        }
        case ASTItem::TAG_Struct: {
            auto& e = item.as_Struct();
            visitStruct(name, vis, e);
            break;
        }
        case ASTItem::TAG_Enum: {
            auto& e = item.as_Enum();
            visitEnum(name, vis, e);
            break;
        }
        case ASTItem::TAG_Union: {
            auto& e = item.as_Union();
            visitUnion(name, vis, e);
            break;
        }
        case ASTItem::TAG_Trait: {
            auto& e = item.as_Trait();
            visitTrait(name, vis, e);
            break;
        }
        case ASTItem::TAG_Function: {
            auto& e = item.as_Function();
            visitFunction(name, vis, e);
            break;
        }
        case ASTItem::TAG_Static: {
            auto& e = item.as_Static();
            visitStatic(name, vis, e);
            break;
        }
    }
}
