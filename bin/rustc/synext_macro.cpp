#include "synext_macro.h"
#include "synext_macro.h"

#include "common.h"
#include "synext.h"
#include "hir_asm.h"
#include "hir_hir.h"
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "parse_lex.h"
#include "expand_cfg.h"
#include "wire_board.h"
#include "parse_common.h"
#include "trans_target.h"
#include "expand_common.h"
#include "parse_ttstream.h"
#include "parse_tokentree.h"
#include "parse_parseerror.h"
#include "parse_interpolated_fragment.h"

#include <cctype>
#include <string_view>

namespace {
    struct FmtArgs {
        enum class Align {
            Unspec,
            Left,
            Center,
            Right,
        };
        enum class Sign {
            Unspec,
            Plus,
            Minus,
        };
        enum class Debug {
            Normal,
            LowerHex,
            UpperHex,
        };

        Align align = Align::Unspec;
        u32 alignChar = ' ';

        Sign sign = Sign::Unspec;
        bool alternate = false;
        bool zeroPad = false;

        Debug debugTy = Debug::Normal;

        bool widthIsArg = false;
        unsigned int width = 0;

        bool precSet = false;
        bool precIsArg = false;
        unsigned int prec = 0;

        bool operator==(const FmtArgs& x) const;

        bool operator!=(const FmtArgs& x) const;

        friend std::ostream& operator<<(std::ostream& os, const FmtArgs& x) {
            os << "Align(";
            switch (x.align) {
                case Align::Unspec:
                    os << "-";
                    break;
                case Align::Left:
                    os << "<";
                    break;
                case Align::Center:
                    os << "^";
                    break;
                case Align::Right:
                    os << ">";
                    break;
            }
            os << "'" << x.alignChar << "'";
            os << ")";
            os << "Sign(";
            switch (x.sign) {
                case Sign::Unspec:
                    os << " ";
                    break;
                case Sign::Plus:
                    os << "+";
                    break;
                case Sign::Minus:
                    os << "-";
                    break;
            }
            if (x.alternate) {
                os << "#";
            }
            if (x.zeroPad) {
                os << "0";
            }
            os << ")";
            os << "Width(" << (x.widthIsArg ? "$" : "") << x.width << ")";
            os << "Prec(" << (x.precIsArg ? "$" : "") << x.prec << ")";
            return os;
        }
    };

    struct FmtFrag {
        std::string leadingText;

        unsigned int argIndex;

        const char* traitName;

        // TODO: Support case where this hasn't been edited (telling the formatter that it has nothing to apply)

        FmtArgs args;
    };

    struct CTraceMacrosExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard&, const ASTCrate&, const TokenTree& tt, ASTModule&) override;
    };

    struct CLogSyntaxExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard&, const ASTCrate&, const TokenTree& tt, ASTModule&) override;
    };

    struct CPatternTypeExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard&, const ASTCrate&, const TokenTree& tt, ASTModule&) override;
    };

    struct CIterExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CLlvmAsmExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CAsmExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CGlobalAsmExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CNakedAsmExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct GenericAssertCaptureVisitor: public ASTNodeVisitor {
        struct Capture {
            ASTPath path;
            RcString name;
            RcString captureName;
            RcString localBindName;
            bool deferred;
        };

        ThinVector<Capture> captures;

        GenericAssertCaptureVisitor(RcString coreCrate, Ident::Hygiene hygiene);

        void manage(ASTExprNodeP& node);

        void visit(ASTExprNodeArray& node) override;

        void visit(ASTExprNodeBinOp& node) override;

        void visit(ASTExprNodeCallPath& node) override;

        void visit(ASTExprNodeCallMethod& node) override;

        void visit(ASTExprNodeCallObject& node) override;

        void visit(ASTExprNodeCast& node) override;

        void visit(ASTExprNodeDeref& node) override;

        void visit(ASTExprNodeIf& node) override;

        void visit(ASTExprNodeIndex& node) override;

        void visit(ASTExprNodeLetBinding& node) override;

        void visit(ASTExprNodeMatch& node) override;

        void visit(ASTExprNodeUniOp& node) override;

        void visit(ASTExprNodeNamedValue& node) override;

        void visit(ASTExprNodeStructLiteral& node) override;

        void visit(ASTExprNodeTuple& node) override;

#define NO_GENERIC_ASSERT_CAPTURE(Node) \
    void visit(Node&) override {        \
    }
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeBlock);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeAsyncBlock);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeGeneratorBlock);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeTry);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeMacro);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeAsm);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeAsm2);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeFlow);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeAssign);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeLoop);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeFor);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeWhile);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeWildcardPattern);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeInteger);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeFloat);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeBool);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeString);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeByteString);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeCString);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeSuffixedLiteral);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeClosure);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeStructLiteralPattern);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeField);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeTypeAnnotation);
        NO_GENERIC_ASSERT_CAPTURE(ASTExprNodeMacroDefinition);
#undef NO_GENERIC_ASSERT_CAPTURE

        ASTExprNodeP makeTryCapture(RcString captureName, RcString localBindName, const Span& sp) const;

        ASTPath generatedPath(RcString name) const;

        ASTExprNodeP makeGeneratedValue(RcString name, const Span& sp) const;

        RcString coreCrate;
        Ident::Hygiene hygiene;
        ASTExprNodeP* current = nullptr;
        bool consumed = true;
    };

    struct CExpanderAssert: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderCompileError: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CConcatExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CConcatBytesExpander: public ExpandProcMacro {
        static char getArrayByte(const Span& sp, const ASTExprNode& node);

        static void append(const Span& sp, std::string& output, const ASTExprNode& node);

        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CConcatIdentsExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderEnv: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderOptionEnv: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderFile: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderLine: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderColumn: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderUnstableColumn: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderModulePath: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CFormatArgsExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CConstFormatArgsExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CFormatArgsNlExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CIncludeExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CIncludeBytesExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CIncludeStrExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderPanic: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderUnreachable: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderRegisterDiagnostic: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderDiagnosticUsed: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpanderBuildDiagnosticArray: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    struct CExpander: public ExpandProcMacro {
        std::unique_ptr<TokenStream> expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) override;
    };

    std::unique_ptr<TokenStream> makeMacroExpansionPlaceholder(const Span& sp) {
        auto rv = box$(TTStreamO(sp, ParseState(), TokenTree()));
        rv->markMacroExpansionPlaceholder();
        return rv;
    }

    std::string getString(const Span& sp, TokenStream& lex, const ASTCrate& crate, ASTModule& mod) {
        auto n = ExpandParseAndExpandExprVal(crate, mod, lex);

        auto* formatStringNp = cast<ASTExprNodeString>(&*n);
        if (!formatStringNp) {
            ERROR(sp, E0000, "asm! requires a string literal - got " << *n);
        }
        return mv$(formatStringNp->value);
    }

    RcString getTokIdentRword(TokenStream& lex) {
        Token tok;
        GET_TOK(tok, lex);
        if (tok.type() == TOK_IDENT) {
            return tok.ident().name;
        }
        if (Token::typeIsRword(tok.type())) {
            return tok.toStr().c_str();
        }
        parseErrorUnexpected(lex, tok, TOK_IDENT);
    }

    AsmRegisterClass getRegClassX8664(const Span& sp, const RcString& str) {
        if (str == "reg") {
            return AsmRegisterClass::x86Reg;
        }
        if (str == "reg_abcd") {
            return AsmRegisterClass::x86RegAbcd;
        }
        if (str == "reg_byte") {
            return AsmRegisterClass::x86RegByte;
        }
        if (str == "kreg") {
            return AsmRegisterClass::x86Kreg;
        }
        if (str == "xmm_reg") {
            return AsmRegisterClass::x86Xmm;
        }
        if (str == "ymm_reg") {
            return AsmRegisterClass::x86Ymm;
        }
        if (str == "zmm_reg") {
            return AsmRegisterClass::x86Zmm;
        }
        ERROR(sp, E0000, "Unknown register for x86/x86-64 - `" << str << "`");
    }

    AsmRegisterClass getRegClassRiscv(const Span& sp, const RcString& str) {
        if (str == "reg") {
            return AsmRegisterClass::riscvReg;
        }
        if (str == "freg") {
            return AsmRegisterClass::riscvFreg;
        }
        ERROR(sp, E0000, "Unknown register for riscv64 - `" << str << "`");
    }

    AsmRegisterClass getRegClass(const WireBoard& wb, const Span& sp, const RcString& str) {
        if (TargetGetCurSpec(wb).arch.name == "x86_64") {
            return getRegClassX8664(sp, str);
        }
        if (TargetGetCurSpec(wb).arch.name == "x86") {
            return getRegClassX8664(sp, str);
        }
        if (TargetGetCurSpec(wb).arch.name == "riscv64") {
            return getRegClassRiscv(sp, str);
        }
        ERROR(sp, E0000, "Unknown architecture for asm!");
    }

    const char* x86ReservedRegister(const std::string& name) {
        static const std::pair<const char*, const char*> reserved[] = {
            {"bp", "the frame pointer"},
            {"ebp", "the frame pointer"},
            {"rbp", "the frame pointer"},
            {"sp", "the stack pointer"},
            {"esp", "the stack pointer"},
            {"rsp", "the stack pointer"},
            {"ip", "the instruction pointer"},
            {"eip", "the instruction pointer"},
            {"rip", "the instruction pointer"},
        };
        for (const auto& entry : reserved) {
            if (name == entry.first) {
                return entry.second;
            }
        }
        return nullptr;
    }

    std::string canonicalX86Register(const std::string& name, bool is64Bit) {
        static const std::pair<const char*, const char*> aliases[] = {
            {"al", "rax"},
            {"ah", "rax"},
            {"ax", "rax"},
            {"eax", "rax"},
            {"rax", "rax"},
            {"bl", "rbx"},
            {"bh", "rbx"},
            {"bx", "rbx"},
            {"ebx", "rbx"},
            {"rbx", "rbx"},
            {"cl", "rcx"},
            {"ch", "rcx"},
            {"cx", "rcx"},
            {"ecx", "rcx"},
            {"rcx", "rcx"},
            {"dl", "rdx"},
            {"dh", "rdx"},
            {"dx", "rdx"},
            {"edx", "rdx"},
            {"rdx", "rdx"},
            {"sil", "rsi"},
            {"si", "rsi"},
            {"esi", "rsi"},
            {"rsi", "rsi"},
            {"dil", "rdi"},
            {"di", "rdi"},
            {"edi", "rdi"},
            {"rdi", "rdi"},
        };
        for (const auto& alias : aliases) {
            if (name == alias.first) {
                if (!is64Bit && alias.second[0] == 'r') {
                    return std::string("e") + (alias.second + 1);
                }
                return alias.second;
            }
        }
        if (name.size() >= 4 && (name[0] == 'x' || name[0] == 'y' || name[0] == 'z') && name.substr(1, 2) == "mm") {
            return std::string("zmm") + name.substr(3);
        }
        if (name == "st" || name == "st(0)") {
            return "st(0)";
        }
        if (is64Bit && name.size() >= 2 && name[0] == 'r' && std::isdigit(static_cast<unsigned char>(name[1]))) {
            auto end = name.find_first_not_of("0123456789", 1);
            return name.substr(0, end);
        }
        return name;
    }

    std::vector<std::string> getClobberAbiRegisters(const WireBoard& wb, const Span& sp, const std::string& abi) {
        const auto& arch = TargetGetCurSpec(wb).arch.name;
        if (arch == "x86_64") {
            const bool sysv = abi == "C" || abi == "system" || abi == "sysv64";
            const bool win = abi == "win64" || abi == "efiapi";
            if (!sysv && !win) {
                ERROR(sp, E0000, "Unsupported clobber ABI `" << abi << "` for x86-64");
            }
            std::vector<std::string> rv = {"rax", "rcx", "rdx"};
            if (sysv) {
                rv.push_back("rsi");
                rv.push_back("rdi");
            }
            for (unsigned i = 8; i <= 11; i++) {
                rv.push_back(FMT("r" << i));
            }
            for (unsigned i = 0; i <= 15; i++) {
                rv.push_back(FMT("xmm" << i));
            }
            for (unsigned i = 16; i <= 31; i++) {
                rv.push_back(FMT("zmm" << i));
            }
            for (unsigned i = 0; i <= 7; i++) {
                rv.push_back(FMT("k" << i));
                rv.push_back(FMT("mm" << i));
                rv.push_back(i == 0 ? "st" : FMT("st(" << i << ")"));
                rv.push_back(FMT("tmm" << i));
            }
            return rv;
        }
        if (arch == "x86") {
            if (abi != "C" && abi != "system" && abi != "efiapi" && abi != "cdecl" && abi != "stdcall" && abi != "fastcall") {
                ERROR(sp, E0000, "Unsupported clobber ABI `" << abi << "` for x86");
            }
            std::vector<std::string> rv = {"eax", "ecx", "edx"};
            for (unsigned i = 0; i <= 7; i++) {
                rv.push_back(FMT("xmm" << i));
                rv.push_back(FMT("k" << i));
                rv.push_back(FMT("mm" << i));
                rv.push_back(i == 0 ? "st" : FMT("st(" << i << ")"));
            }
            return rv;
        }
        if (arch == "riscv64") {
            if (abi != "C" && abi != "system" && abi != "efiapi") {
                ERROR(sp, E0000, "Unsupported clobber ABI `" << abi << "` for RISC-V");
            }
            std::vector<std::string> rv;
            for (auto i : {1u, 5u, 6u, 7u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 28u, 29u, 30u, 31u}) {
                rv.push_back(FMT("x" << i));
            }
            for (auto i : {0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 10u, 11u, 12u, 13u, 14u, 15u, 16u, 17u, 28u, 29u, 30u, 31u}) {
                rv.push_back(FMT("f" << i));
            }
            for (unsigned i = 0; i <= 31; i++) {
                rv.push_back(FMT("v" << i));
            }
            return rv;
        }
        ERROR(sp, E0000, "clobber_abi is unsupported for target architecture `" << arch << "`");
    }

    std::string getString(const Span& sp, const WireBoard& wb, const ASTCrate& crate, ASTModule& mod, const TokenTree& tt) {
        auto lex = TTStream(sp, ParseState(), tt);
        lex.parseState().wb = &wb;

        auto n = ParseExprVal(lex);
        ASSERT_BUG(sp, n, "No expression returned");
        if (lex.lookahead(0) == TOK_COMMA) {
            lex.getToken();
        }
        if (lex.lookahead(0) != TOK_EOF) {
            ERROR(sp, E0000, "Unexpected token after string literal - " << lex.getToken());
        }
        ExpandBareExpr(*lex.parseState().wb, crate, mod, n);

        auto* stringNp = cast<ASTExprNodeString>(&*n);
        if (!stringNp) {
            ERROR(sp, E0000, "Expected a string literal - got " << *n);
        }
        return mv$(stringNp->value);
    }

    u32 parseUtf8(const char* s, int& outLen) {
        u8 v1 = s[0];
        if (v1 < 0x80) {
            outLen = 1;
            return v1;
        } else if ((v1 & 0xC0) == 0x80) {
            outLen = 1;
            return 0xFFFE;
        } else if ((v1 & 0xE0) == 0xC0) {
            outLen = 2;

            u8 e1 = s[1];
            if ((e1 & 0xC0) != 0x80) {
                return 0xFFFE;
            }

            u32 outval = ((v1 & 0x1F) << 6) | ((e1 & 0x3F) << 0);
            return outval;
        } else if ((v1 & 0xF0) == 0xE0) {
            outLen = 3;
            u8 e1 = s[1];
            if ((e1 & 0xC0) != 0x80) {
                return 0xFFFE;
            }
            u8 e2 = s[2];
            if ((e2 & 0xC0) != 0x80) {
                return 0xFFFE;
            }

            u32 outval = ((v1 & 0x0F) << 12) | ((e1 & 0x3F) << 6) | ((e2 & 0x3F) << 0);
            return outval;
        } else if ((v1 & 0xF8) == 0xF0) {
            outLen = 4;
            u8 e1 = s[1];
            if ((e1 & 0xC0) != 0x80) {
                return 0xFFFE;
            }
            u8 e2 = s[2];
            if ((e2 & 0xC0) != 0x80) {
                return 0xFFFE;
            }
            u8 e3 = s[3];
            if ((e3 & 0xC0) != 0x80) {
                return 0xFFFE;
            }

            u32 outval = ((v1 & 0x07) << 18) | ((e1 & 0x3F) << 12) | ((e2 & 0x3F) << 6) | ((e3 & 0x3F) << 0);
            return outval;
        } else {
            UNREACHABLE();
        }
    }

    std::tuple<std::vector<FmtFrag>, std::string> parseFormatString(const Span& sp, const std::string& formatString, std::map<RcString, unsigned int>& named, unsigned int nFree, std::vector<TokenTree>& namedArgs, const Ident::Hygiene& hygiene) {
        unsigned int nextFree = 0;
        const unsigned int nPositional = nFree + static_cast<unsigned>(namedArgs.size());

        std::vector<FmtFrag> frags;
        std::string curLiteral;

        auto getNamed = [&](RcString ident) -> unsigned {
            auto it = named.find(ident);
            if (it == named.end()) {
                it = named.insert(std::make_pair(ident, static_cast<unsigned>(namedArgs.size()))).first;
                // TODO: Create a token with span information pointing to this location in the string.
                if (ident == "self") {
                    namedArgs.push_back(Token(TOK_RWORD_SELF));
                } else {
                    namedArgs.push_back(Token(TOK_IDENT, Ident(hygiene, ident)));
                }
            }
            return nFree + it->second;
        };

        const char* s = formatString.c_str();
        const char* const sEnd = s + formatString.length();
        auto skipWhitespace = [&]() {
            while (s < sEnd && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) {
                s++;
            }
        };
        for (; s < sEnd; s++) {
            if (*s != '{') {
                if (*s == '}') {
                    s++;
                    if (*s != '}') {
                        // TODO: Error? Warning?
                        s--;
                    }
                    curLiteral += '}';
                } else {
                    curLiteral += *s;
                }
            } else {
                s++;
                if (*s == '{') {
                    curLiteral += '{';
                    continue;
                }
                skipWhitespace();

                const char* s2 = s;
                while (s2 < sEnd && *s2 != '}') {
                    s2++;
                }
                auto fmtFragStr = std::string_view{s, s2};

                unsigned int index = ~0u;
                const char* traitName;
                FmtArgs args;

                if (*s != ':' && *s != '}') {
                    if (isdigit(*s)) {
                        unsigned int argIdx = 0;
                        do {
                            argIdx *= 10;
                            argIdx += *s - '0';
                            s++;
                        } while (isdigit(*s));
                        if (argIdx >= nPositional) {
                            ERROR(sp, E0000, "Positional argument " << argIdx << " out of range in \"" << formatString << "\"");
                        }
                        index = argIdx;
                    } else {
                        const char* start = s;
                        while (isalnum(*s) || *s == '_' || (*s < 0 || *s > 127)) {
                            s++;
                        }
                        index = getNamed(RcString::newInterned(start, s - start));
                    }
                } else {
                }

                skipWhitespace();

                if (*s == ':') {
                    s++;

                    {
                        int nextCI;
                        u32 ch = parseUtf8(s, nextCI);
                        char nextC = s[nextCI];
                        if (s + nextCI <= sEnd && ch != '}' && (nextC == '<' || nextC == '^' || nextC == '>')) {
                            args.alignChar = ch;
                            s += nextCI;
                        }
                    }
                    if (*s == '<') {
                        args.align = FmtArgs::Align::Left;
                        s++;
                    } else if (*s == '^') {
                        args.align = FmtArgs::Align::Center;
                        s++;
                    } else if (*s == '>') {
                        args.align = FmtArgs::Align::Right;
                        s++;
                    } else {
                    }

                    if (*s == '+') {
                        args.sign = FmtArgs::Sign::Plus;
                        s++;
                    } else if (*s == '-') {
                        args.sign = FmtArgs::Sign::Minus;
                        s++;
                    } else {
                        args.sign = FmtArgs::Sign::Unspec;
                    }

                    if (*s == '#') {
                        args.alternate = true;
                        s++;
                    } else {
                    }

                    if (*s == '0' && s[1] != '$') {
                        args.zeroPad = true;
                        s++;
                    } else {
                    }

                    if (std::isdigit(*s) /*|| *s == '*'*/) {
                        unsigned int val = 0;
                        while (std::isdigit(*s)) {
                            val *= 10;
                            val += *s - '0';
                            s++;
                        }
                        args.width = val;

                        if (*s == '$') {
                            args.widthIsArg = true;
                            s++;
                        } else {
                        }
                    } else if (std::isalpha(*s) || *s == '_') {
                        const char* start = s;
                        while (isalnum(*s) || *s == '_' || (*s < 0 || *s > 127)) {
                            s++;
                        }
                        if (*s == '$') {
                            args.width = getNamed(RcString::newInterned(start, s - start));
                            args.widthIsArg = true;

                            s++;
                        } else {
                            s = start;
                        }
                    } else {
                    }
                    if (*s == '.') {
                        s++;
                        args.precSet = true;
                        if (*s == '*') {
                            args.precIsArg = true;
                            if (nextFree == nPositional) {
                                ERROR(sp, E0000, "Not enough arguments passed, expected at least " << nPositional + 1);
                            }
                            args.prec = nextFree;
                            nextFree++;
                            s++;
                        } else if (std::isdigit(*s)) {
                            unsigned int val = 0;
                            while (std::isdigit(*s)) {
                                val *= 10;
                                val += *s - '0';
                                s++;
                            }
                            args.prec = val;

                            if (*s == '$') {
                                args.precIsArg = true;
                                s++;
                            } else {
                            }
                        } else if (std::isalpha(*s) || *s == '_') {
                            const char* start = s;
                            while (s != sEnd && (isalnum(*s) || *s == '_' || (*s < 0 || *s > 127))) {
                                s++;
                            }
                            if (*s == '$') {
                                args.prec = getNamed(RcString::newInterned(start, s - start));
                                args.precIsArg = true;

                                s++;
                            } else {
                                s = start;
                            }
                        } else {
                            ERROR(sp, E0000, "Unexpected character in precision");
                        }
                    }

                    skipWhitespace();

                    if (s == sEnd) {
                        ERROR(sp, E0000, "Unexpected end of formatting string");
                    }

                    if (s[0] == '}') {
                        traitName = "Display";
                    } else if (s[1] == '}') {
                        switch (s[0]) {
                            default:
                                ERROR(sp, E0000, "Unknown formatting type specifier '" << *s << "'");
                            case '?':
                                s++;
                                traitName = "Debug";
                                break;
                            case 'b':
                                s++;
                                traitName = "Binary";
                                break;
                            case 'o':
                                s++;
                                traitName = "Octal";
                                break;
                            case 'x':
                                s++;
                                traitName = "LowerHex";
                                break;
                            case 'X':
                                s++;
                                traitName = "UpperHex";
                                break;
                            case 'p':
                                s++;
                                traitName = "Pointer";
                                break;
                            case 'e':
                                s++;
                                traitName = "LowerExp";
                                break;
                            case 'E':
                                s++;
                                traitName = "UpperExp";
                                break;
                        }
                        assert(*s == '}');
                    } else {
                        if (strncmp(s, "x?}", 3) == 0) {
                            args.debugTy = FmtArgs::Debug::LowerHex;
                            traitName = "Debug";
                            s += 2;
                        } else if (strncmp(s, "X?}", 3) == 0) {
                            args.debugTy = FmtArgs::Debug::UpperHex;
                            traitName = "Debug";
                            s += 2;
                        } else {
                            TODO(sp, "Parse formatting fragment at \"" << fmtFragStr << "\" (long type) - s=...\"" << s << "\"");
                        }
                    }
                } else {
                    if (*s != '}') {
                        ERROR(sp, E0000, "Malformed formatting fragment, unexpected " << *s);
                    }
                    traitName = "Display";
                }

                if (index == ~0u) {
                    if (nextFree == nPositional) {
                        ERROR(sp, E0000, "Not enough arguments passed, expected at least " << nPositional + 1);
                    }
                    index = nextFree;
                    nextFree++;
                }

                frags.push_back(FmtFrag{mv$(curLiteral), index, traitName, mv$(args)});
            }
        }

        return std::make_tuple(mv$(frags), mv$(curLiteral));
    }

    Token ident(const char* s) {
        return Token(TOK_IDENT, RcString::newInterned(s));
    }

    void pushPath(std::vector<TokenTree>& toks, const ASTCrate& crate, std::initializer_list<const char*> il) {
        ASTAbsolutePath ap;
        // TODO: Inject a path fragment (interpolated path), to avoid edition parsing quirks
        switch (crate.loadStd) {
            case ASTCrate::LOAD_NONE:
                break;
            case ASTCrate::LOAD_CORE:
                ASSERT_BUG(Span(), crate.extCratenameCore != "", "");
                ap.crate = crate.extCratenameCore;
                break;
            case ASTCrate::LOAD_STD:
                ASSERT_BUG(Span(), crate.extCratenameCore != "", "");
                ap.crate = crate.extCratenameCore;
                break;
        }
        for (auto ent : il) {
            // TODO: This could be slow (looking up the interned string), but most of these are repeated a LOT
            ap.nodes.push_back(RcString::newInterned(ent));
        }
        toks.push_back(Token(InterpolatedFragment(std::move(ap))));
    }

    void pushToks(std::vector<TokenTree>& toks, Token t1) {
        toks.push_back(mv$(t1));
    }

    void pushToks(std::vector<TokenTree>& toks, Token t1, Token t2) {
        toks.push_back(mv$(t1));
        toks.push_back(mv$(t2));
    }

    void pushToks(std::vector<TokenTree>& toks, Token t1, Token t2, Token t3, Token t4) {
        toks.push_back(mv$(t1));
        toks.push_back(mv$(t2));
        toks.push_back(mv$(t3));
        toks.push_back(mv$(t4));
    }

    std::unique_ptr<TokenStream> expandFormatArgs(const Span& sp, const WireBoard& wb, const ASTCrate& crate, TTStream& lex, bool addNewline) {
        Token tok;

        auto formatStringNode = ParseExprVal(lex);
        ASSERT_BUG(sp, formatStringNode, "No expression returned");
        ExpandBareExpr(*lex.parseState().wb, crate, lex.parseState().getCurrentMod(), formatStringNode);

        auto* formatStringNp = cast<ASTExprNodeString>(&*formatStringNode);
        if (!formatStringNp) {
            ERROR(sp, E0000, "format_args! requires a string literal - got " << *formatStringNode);
        }
        const auto& formatStringSp = formatStringNp->span();
        const auto& formatString = formatStringNp->value;
        auto h = formatStringNp->hygiene;

        std::map<RcString, unsigned int> namedArgsIndex;
        std::vector<TokenTree> namedArgs;
        std::vector<TokenTree> freeArgs;

        while (GET_TOK(tok, lex) == TOK_COMMA) {
            if (lex.lookahead(0) == TOK_EOF) {
                GET_TOK(tok, lex);
                break;
            }

            if ((lex.lookahead(0) == TOK_IDENT || Token::typeIsRword(lex.lookahead(0))) && lex.lookahead(1) == TOK_EQUAL) {
                GET_TOK(tok, lex);
                auto name = tok.type() == TOK_IDENT ? tok.ident().name : RcString::newInterned(tok.toStr());

                GET_CHECK_TOK(tok, lex, TOK_EQUAL);

                auto exprTt = TokenTree(Token(InterpolatedFragment(InterpolatedFragment::EXPR, ParseExpr0(lex).release())));

                auto insRv = namedArgsIndex.insert(std::make_pair(mv$(name), static_cast<unsigned>(namedArgs.size())));
                if (insRv.second == false) {
                    ERROR(sp, E0000, "Duplicate definition of named argument `" << insRv.first->first << "`");
                }
                namedArgs.push_back(mv$(exprTt));
            } else {
                auto exprTt = TokenTree(Token(InterpolatedFragment(InterpolatedFragment::EXPR, ParseExpr0(lex).release())));
                freeArgs.push_back(mv$(exprTt));
            }
        }
        CHECK_TOK(tok, TOK_EOF);

        std::vector<FmtFrag> fragments;
        std::string tail;
        std::tie(fragments, tail) = parseFormatString(formatStringSp, formatString, namedArgsIndex, freeArgs.size(), namedArgs, h);
        if (addNewline) {
            tail += "\n";
        }
        if (lex.parseState().wb->settings->fmtDebug == Settings::FmtDebug::None) {
            std::vector<FmtFrag> kept;
            std::string pending;
            for (auto& frag : fragments) {
                pending += frag.leadingText;
                if (std::strcmp(frag.traitName, "Debug") == 0) {
                    continue;
                }
                frag.leadingText = mv$(pending);
                pending.clear();
                kept.push_back(mv$(frag));
            }
            tail = pending + tail;
            fragments = mv$(kept);
        }

        bool isSimple = true;
        for (unsigned int i = 0; i < fragments.size(); i++) {
            if (fragments[i].argIndex != i) {
                isSimple = false;
            }
            if (fragments[i].args != FmtArgs{}) {
                isSimple = false;
            }
        }

        std::vector<TokenTree> toks;
        toks.push_back(TokenTree(TOK_RWORD_MATCH));
        toks.push_back(TokenTree(TOK_PAREN_OPEN));
        for (auto& arg : freeArgs) {
            toks.push_back(TokenTree(TOK_AMP));
            toks.push_back(mv$(arg));
            toks.push_back(TokenTree(TOK_COMMA));
        }
        for (auto& arg : namedArgs) {
            toks.push_back(TokenTree(TOK_AMP));
            toks.push_back(mv$(arg));
            toks.push_back(TokenTree(TOK_COMMA));
        }
        toks.push_back(TokenTree(TOK_PAREN_CLOSE));
        toks.push_back(TokenTree(TOK_BRACE_OPEN));
        toks.push_back(TokenTree(TOK_PAREN_OPEN));
        for (unsigned int i = 0; i < freeArgs.size() + namedArgs.size(); i++) {
            toks.push_back(ident(FMT("a" << i).c_str()));
            toks.push_back(TokenTree(TOK_COMMA));
        }
        toks.push_back(TokenTree(TOK_PAREN_CLOSE));
        toks.push_back(TokenTree(TOK_FATARROW));
        toks.push_back(TokenTree(TOK_BRACE_OPEN));

        {
            toks.push_back(TokenTree(TOK_RWORD_STATIC));
            toks.push_back(ident("FRAGMENTS"));
            toks.push_back(TokenTree(TOK_COLON));

            toks.push_back(TokenTree(TOK_SQUARE_OPEN));
            toks.push_back(Token(TOK_AMP));
            toks.push_back(Token(TOK_LIFETIME, RcString::newInterned("static")));
            toks.push_back(ident("str"));
            toks.push_back(Token(TOK_SEMICOLON));
            toks.push_back(Token(U128(fragments.size() + 1), CORETYPE_UINT));
            toks.push_back(TokenTree(TOK_SQUARE_CLOSE));

            toks.push_back(Token(TOK_EQUAL));

            toks.push_back(TokenTree(TOK_SQUARE_OPEN));
            for (const auto& frag : fragments) {
                toks.push_back(Token(TOK_STRING, frag.leadingText, h));
                toks.push_back(TokenTree(TOK_COMMA));
            }
            toks.push_back(Token(TOK_STRING, tail, h));
            toks.push_back(TokenTree(TOK_SQUARE_CLOSE));

            toks.push_back(Token(TOK_SEMICOLON));
        }

        struct H {
            static void argumentList(std::vector<TokenTree>& toks, const std::vector<FmtFrag>& fragments, const ASTCrate& crate) {
                toks.push_back(TokenTree(TOK_AMP));
                toks.push_back(TokenTree(TOK_SQUARE_OPEN));
                for (const auto& frag : fragments) {
                    std::stringstream newFnSs;
                    newFnSs << "new";
                    for (const char* s = frag.traitName; *s; s++) {
                        if (isupper(*s)) {
                            newFnSs << "_" << char(tolower(*s));
                        } else {
                            newFnSs << *s;
                        }
                    }
                    pushPath(toks, crate, {"fmt", "rt", "Argument", newFnSs.str().c_str()});
                    toks.push_back(Token(TOK_PAREN_OPEN));
                    toks.push_back(ident(FMT("a" << frag.argIndex).c_str()));
                    toks.push_back(Token(TOK_PAREN_CLOSE));
                    toks.push_back(TokenTree(TOK_COMMA));
                }
                toks.push_back(TokenTree(TOK_SQUARE_CLOSE));
            }
        };

        if (isSimple) {
            pushPath(toks, crate, {"fmt", "Arguments", "new_v1"});
            toks.push_back(TokenTree(TOK_PAREN_OPEN));
            {
                toks.push_back(TokenTree(TOK_AMP));
                toks.push_back(ident("FRAGMENTS"));
                toks.push_back(TokenTree(TOK_COMMA));

                H::argumentList(toks, fragments, crate);
            }
            toks.push_back(TokenTree(TOK_PAREN_CLOSE));
        } else {
            pushPath(toks, crate, {"fmt", "Arguments", "new_v1_formatted"});
            toks.push_back(TokenTree(TOK_PAREN_OPEN));
            {
                toks.push_back(TokenTree(TOK_AMP));
                toks.push_back(ident("FRAGMENTS"));
                toks.push_back(TokenTree(TOK_COMMA));

                // TODO: Fragments to format

                H::argumentList(toks, fragments, crate);
                toks.push_back(TokenTree(TOK_COMMA));

                toks.push_back(TokenTree(TOK_AMP));
                toks.push_back(TokenTree(TOK_SQUARE_OPEN));
                for (const auto& frag : fragments) {
                    pushPath(toks, crate, {"fmt", "rt", "Placeholder"});
                    toks.push_back(TokenTree(TOK_BRACE_OPEN));

                    pushToks(toks, ident("position"), TOK_COLON);
                    pushToks(toks, Token(U128(&frag - fragments.data()), CORETYPE_UINT));
                    pushToks(toks, TOK_COMMA);

                    {
                        pushToks(toks, ident("flags"), TOK_COLON);

                        struct Flag {
                            enum V {
                                SignPlus,
                                SignMinus,
                                Alternate,
                                SignAwareZeroPad,
                                DebugLowerHex,
                                DebugUpperHex,
                            };
                        };

                        u64 flags = 0;
                        switch (frag.args.sign) {
                            case FmtArgs::Sign::Unspec:
                                break;
                            case FmtArgs::Sign::Plus:
                                flags |= 1 << Flag::SignPlus;
                                break;
                            case FmtArgs::Sign::Minus:
                                flags |= 1 << Flag::SignMinus;
                                break;
                        }
                        if (frag.args.alternate) {
                            flags |= 1 << Flag::Alternate;
                        }
                        if (frag.args.zeroPad) {
                            flags |= 1 << Flag::SignAwareZeroPad;
                        }
                        switch (frag.args.debugTy) {
                            case FmtArgs::Debug::Normal:
                                break;
                            case FmtArgs::Debug::LowerHex:
                                flags |= 1 << Flag::DebugLowerHex;
                                break;
                            case FmtArgs::Debug::UpperHex:
                                flags |= 1 << Flag::DebugUpperHex;
                                break;
                        }
                        flags <<= 21;
                        flags |= frag.args.alignChar & 0x1FFFFF;

                        if (frag.args.widthIsArg || frag.args.width != 0) {
                            flags |= 1 << 27;
                        }
                        if (frag.args.precSet) {
                            flags |= 1 << 28;
                        }

                        switch (frag.args.align) {
                            case FmtArgs::Align::Unspec:
                                flags |= 3 << 29;
                                break;
                            case FmtArgs::Align::Left:
                                flags |= 0 << 29;
                                break;
                            case FmtArgs::Align::Right:
                                flags |= 1 << 29;
                                break;
                            case FmtArgs::Align::Center:
                                flags |= 2 << 29;
                                break;
                        }

                        flags |= 1 << 31;
                        pushToks(toks, Token(U128(flags), CORETYPE_U32));
                        pushToks(toks, TOK_COMMA);
                    }
                    {
                        auto pushPathCount = [&](const char* variant) {
                            pushPath(toks, crate, {"fmt", "rt", "Count", variant});
                        };

                        pushToks(toks, ident("precision"), TOK_COLON);
                        if (frag.args.precSet) {
                            pushPathCount("Is");
                            pushToks(toks, TOK_PAREN_OPEN);
                            if (frag.args.precIsArg) {
                                pushToks(toks, TOK_STAR, ident(FMT("a" << frag.args.prec).c_str()));
                                pushToks(toks, TOK_RWORD_AS, ident("u16"));
                            } else {
                                pushToks(toks, Token(U128(frag.args.prec), CORETYPE_U16));
                            }
                            toks.push_back(TokenTree(TOK_PAREN_CLOSE));
                        } else {
                            pushPathCount("Implied");
                        }
                        toks.push_back(TokenTree(TOK_COMMA));

                        pushToks(toks, ident("width"), TOK_COLON);
                        if (frag.args.widthIsArg || frag.args.width != 0) {
                            pushPathCount("Is");
                            pushToks(toks, TOK_PAREN_OPEN);
                            if (frag.args.widthIsArg) {
                                pushToks(toks, TOK_STAR, ident(FMT("a" << frag.args.width).c_str()));
                                pushToks(toks, TOK_RWORD_AS, ident("u16"));
                            } else {
                                pushToks(toks, Token(U128(frag.args.width), CORETYPE_U16));
                            }
                            toks.push_back(TokenTree(TOK_PAREN_CLOSE));
                        } else {
                            pushPathCount("Implied");
                        }
                        toks.push_back(TokenTree(TOK_COMMA));
                    }

                    toks.push_back(TokenTree(TOK_BRACE_CLOSE));
                    toks.push_back(TokenTree(TOK_COMMA));
                }
                toks.push_back(TokenTree(TOK_SQUARE_CLOSE));
            }
            toks.push_back(TokenTree(TOK_PAREN_CLOSE));
        }

        toks.push_back(TokenTree(TOK_BRACE_CLOSE));
        toks.push_back(TokenTree(TOK_BRACE_CLOSE));

        return box$(TTStreamO(sp, ParseState(), TokenTree(lex.getEdition(), Ident::Hygiene::newScope(wb.id, *crate.hirPool), mv$(toks))));
    }

    std::string includeGetString(const Span& sp, TokenStream& lex, const ASTCrate& crate, ASTModule& mod) {
        auto n = ParseExprVal(lex);
        ASSERT_BUG(sp, n, "No expression returned");
        if (lex.lookahead(0) == TOK_COMMA) {
            lex.getToken();
        }
        ExpandBareExpr(*lex.parseState().wb, crate, mod, n);

        auto* stringNp = cast<ASTExprNodeString>(&*n);
        if (!stringNp) {
            ERROR(sp, E0000, "include! requires a string literal - got " << *n);
        }
        return mv$(stringNp->value);
    }

    std::string getPathRelativeTo(const std::string& basePath, std::string path) {
        if (path[0] == '/') {
            return path;
        }
        if (basePath.size() == 0) {
            return path;
        } else if (basePath.back() == '/') {
            return basePath + path;
        } else {
            auto slash = basePath.find_last_of('/');
            if (slash == std::string::npos) {
                return path;
            } else {
                slash += 1;
                std::string rv;
                rv.reserve(slash + path.size());
                rv.append(basePath.begin(), basePath.begin() + slash);
                rv.append(path.begin(), path.end());
                return rv;
            }
        }
    }
}

#undef CMP

;

// TODO: include_str! and include_bytes!

void RegisterBuiltinMacros(ExpandRegistry& registry) {
    registry.addMacro<CTraceMacrosExpander>("trace_macros");
    registry.addMacro<CLogSyntaxExpander>("log_syntax");
    registry.addMacro<CPatternTypeExpander>("pattern_type");
    registry.addMacro<CIterExpander>("iter");
    registry.addMacro<CLlvmAsmExpander>("llvm_asm");
    registry.addMacro<CAsmExpander>("asm");
    registry.addMacro<CGlobalAsmExpander>("global_asm");
    registry.addMacro<CNakedAsmExpander>("naked_asm");
    registry.addMacro<CExpanderAssert>("assert");
    registry.addMacro<CExpanderCompileError>("compile_error");
    registry.addMacro<CConcatExpander>("concat");
    registry.addMacro<CConcatBytesExpander>("concat_bytes");
    registry.addMacro<CConcatIdentsExpander>("concat_idents");
    registry.addMacro<CExpanderEnv>("env");
    registry.addMacro<CExpanderOptionEnv>("option_env");
    registry.addMacro<CExpanderFile>("file");
    registry.addMacro<CExpanderLine>("line");
    registry.addMacro<CExpanderColumn>("column");
    registry.addMacro<CExpanderUnstableColumn>("__rust_unstable_column");
    registry.addMacro<CExpanderModulePath>("module_path");
    registry.addMacro<CFormatArgsExpander>("format_args");
    registry.addMacro<CConstFormatArgsExpander>("const_format_args");
    registry.addMacro<CFormatArgsNlExpander>("format_args_nl");
    registry.addMacro<CIncludeExpander>("include");
    registry.addMacro<CIncludeBytesExpander>("include_bytes");
    registry.addMacro<CIncludeStrExpander>("include_str");
    registry.addMacro<CExpanderPanic>("panic");
    registry.addMacro<CExpanderUnreachable>("unreachable");
    registry.addMacro<CExpanderRegisterDiagnostic>("__register_diagnostic");
    registry.addMacro<CExpanderDiagnosticUsed>("__diagnostic_used");
    registry.addMacro<CExpanderBuildDiagnosticArray>("__build_diagnostic_array");
    registry.addMacro<CExpander>("stringify");
}

std::unique_ptr<TokenStream> ExpandProcMacro::expandIdent(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const RcString& ident, const TokenTree& tt, ASTModule& mod) {
    ERROR(sp, E0000, "macro doesn't take an identifier");
}

auto CTraceMacrosExpander::expand(const Span& sp, const WireBoard&, const ASTCrate&, const TokenTree& tt, ASTModule&) -> std::unique_ptr<TokenStream> {
    auto lex = TTStream(sp, ParseState(), tt);
    const auto setting = lex.getToken();
    if (setting.type() != TOK_RWORD_TRUE && setting.type() != TOK_RWORD_FALSE) {
        ERROR(sp, E0000, "trace_macros! expects `true` or `false`");
    }
    if (lex.lookahead(0) != TOK_EOF) {
        ERROR(sp, E0000, "trace_macros! expects exactly one boolean argument");
    }
    return makeMacroExpansionPlaceholder(sp);
}

auto CLogSyntaxExpander::expand(const Span& sp, const WireBoard&, const ASTCrate&, const TokenTree& tt, ASTModule&) -> std::unique_ptr<TokenStream> {
    auto lex = TTStream(sp, ParseState(), tt);
    bool first = true;
    while (lex.lookahead(0) != TOK_EOF) {
        if (!first) {
            std::cout << ' ';
        }
        std::cout << lex.getToken().toStr();
        first = false;
    }
    std::cout << std::endl;
    return makeMacroExpansionPlaceholder(sp);
}

auto CPatternTypeExpander::expand(const Span& sp, const WireBoard&, const ASTCrate&, const TokenTree& tt, ASTModule&) -> std::unique_ptr<TokenStream> {
    return box$(TTStreamO(sp, ParseState(), tt.clone()));
}

auto CIterExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;
    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().crate = &crate;
    lex.parseState().wb = &wb;
    lex.parseState().module = &mod;

    auto node = ParseExpr0(lex);
    GET_CHECK_TOK(tok, lex, TOK_EOF);

    auto* closure = cast<ASTExprNodeClosure>(node.get());
    if (!closure || closure->isPinned || cast<ASTExprNodeAsyncBlock>(closure->code.get())) {
        ERROR(sp, E0000, "iter! requires a plain closure");
    }

    auto* generator = new ASTExprNodeGeneratorBlock(mv$(closure->code), closure->returnType, true, true);
    generator->setSpan(sp);
    closure->code = ASTExprNodeP(generator);
    closure->returnType = mkType(*crate.pool, sp);

    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(InterpolatedFragment(InterpolatedFragment::EXPR, node.release())))));
}

auto CLlvmAsmExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;
    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;

    auto templateText = getString(sp, lex, crate, mod);
    std::vector<ASTExprNodeAsm::ValRef> outputs;
    std::vector<ASTExprNodeAsm::ValRef> inputs;
    std::vector<std::string> clobbers;
    std::vector<std::string> flags;

    if (lex.lookahead(0) == TOK_DOUBLE_COLON) {
        GET_TOK(tok, lex);
        lex.putback(Token(TOK_COLON));
    } else if (lex.lookahead(0) == TOK_COLON) {
        GET_TOK(tok, lex);

        while (lex.lookahead(0) == TOK_STRING) {
            GET_CHECK_TOK(tok, lex, TOK_STRING);
            auto name = mv$(tok.str());

            GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
            auto val = ParseExpr0(lex);
            GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);

            outputs.push_back(ASTExprNodeAsm::ValRef{mv$(name), mv$(val)});

            if (lex.lookahead(0) != TOK_COMMA) {
                break;
            }

            GET_TOK(tok, lex);
        }
    } else {
    }

    if (lex.lookahead(0) == TOK_DOUBLE_COLON) {
        GET_TOK(tok, lex);
        lex.putback(Token(TOK_COLON));
    } else if (lex.lookahead(0) == TOK_COLON) {
        GET_TOK(tok, lex);

        while (lex.lookahead(0) == TOK_STRING) {
            GET_CHECK_TOK(tok, lex, TOK_STRING);
            auto name = mv$(tok.str());

            GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
            auto val = ParseExpr0(lex);
            GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);

            inputs.push_back(ASTExprNodeAsm::ValRef{mv$(name), mv$(val)});

            if (lex.lookahead(0) != TOK_COMMA) {
                break;
            }
            GET_TOK(tok, lex);
        }
    } else {
    }

    if (lex.lookahead(0) == TOK_DOUBLE_COLON) {
        GET_TOK(tok, lex);
        lex.putback(Token(TOK_COLON));
    } else if (lex.lookahead(0) == TOK_COLON) {
        GET_TOK(tok, lex);

        while (lex.lookahead(0) == TOK_STRING) {
            GET_CHECK_TOK(tok, lex, TOK_STRING);
            clobbers.push_back(mv$(tok.str()));

            if (lex.lookahead(0) != TOK_COMMA) {
                break;
            }
            GET_TOK(tok, lex);
        }
    } else {
    }

    if (lex.lookahead(0) == TOK_DOUBLE_COLON) {
        GET_TOK(tok, lex);
        lex.putback(Token(TOK_COLON));
    } else if (lex.lookahead(0) == TOK_COLON) {
        GET_TOK(tok, lex);

        while (lex.lookahead(0) == TOK_STRING) {
            GET_CHECK_TOK(tok, lex, TOK_STRING);
            flags.push_back(mv$(tok.str()));

            if (lex.lookahead(0) != TOK_COMMA) {
                break;
            }
            GET_TOK(tok, lex);
        }
    } else {
    }

    // trailing `: voltaile` - TODO: Is this valid?
    if (lex.lookahead(0) == TOK_DOUBLE_COLON) {
        GET_TOK(tok, lex);
        lex.putback(Token(TOK_COLON));
    } else if (lex.lookahead(0) == TOK_COLON) {
        GET_TOK(tok, lex);

        if (GET_TOK(tok, lex) == TOK_IDENT && tok.ident() == "volatile") {
            flags.push_back("volatile");
        } else {
            PUTBACK(tok, lex);
        }
    } else {
    }

    if (lex.lookahead(0) != TOK_EOF) {
        ERROR(sp, E0000, "Unexpected token in asm! - " << lex.getToken());
    }

    ASTExprNodeP rv = ASTExprNodeP(new ASTExprNodeAsm{mv$(templateText), mv$(outputs), mv$(inputs), mv$(clobbers), mv$(flags)});
    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(InterpolatedFragment(InterpolatedFragment::EXPR, rv.release())))));
}

auto CAsmExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;
    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;

    std::vector<std::pair<Span, std::string>> rawLines;
    do {
        auto ps = lex.startSpan();
        auto attrs = ParseItemAttrs(lex);
        auto text = getString(sp, lex, crate, mod);
        auto sp = lex.endSpan(ps);
        if (checkCfgAttrs(*lex.parseState().wb->settings, attrs)) {
            rawLines.push_back(std::make_pair(sp, std::move(text)));
        }

        if (lex.lookahead(0) == TOK_EOF) {
            GET_TOK(tok, lex);
            break;
        }
        GET_CHECK_TOK(tok, lex, TOK_COMMA);
    } while (lex.lookahead(0) == TOK_STRING || lex.lookahead(0) == TOK_HASH);

    std::vector<ASTExprNodeAsm2::Param> params;
    std::vector<RcString> names;
    std::vector<std::string> clobberAbis;
    AsmOptions options;
    while (tok.type() == TOK_COMMA) {
        if (lex.lookahead(0) == TOK_EOF) {
            GET_TOK(tok, lex);
            break;
        }

        RcString bindingName;
        auto v = getTokIdentRword(lex);
        if (v == "clobber_abi") {
            GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
            do {
                GET_CHECK_TOK(tok, lex, TOK_STRING);
                clobberAbis.push_back(tok.str());
                if (lex.lookahead(0) == TOK_PAREN_CLOSE) {
                    GET_TOK(tok, lex);
                    break;
                }
            } while (GET_TOK(tok, lex) == TOK_COMMA);
            CHECK_TOK(tok, TOK_PAREN_CLOSE);

            GET_TOK(tok, lex);
            continue;
        }
        if (v == "options") {
            GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
            do {
                GET_CHECK_TOK(tok, lex, TOK_IDENT);

                if (tok.ident().name == "pure") {
                    if (options.pure) {
                        ERROR(lex.pointSpan(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                    }
                    options.pure = 1;
                } else if (tok.ident().name == "nomem") {
                    if (options.nomem) {
                        ERROR(lex.pointSpan(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                    }
                    options.nomem = 1;
                } else if (tok.ident().name == "readonly") {
                    if (options.readonly) {
                        ERROR(lex.pointSpan(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                    }
                    options.readonly = 1;
                } else if (tok.ident().name == "preserves_flags") {
                    if (options.preservesFlags) {
                        ERROR(lex.pointSpan(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                    }
                    options.preservesFlags = 1;
                } else if (tok.ident().name == "noreturn") {
                    if (options.noreturn) {
                        ERROR(lex.pointSpan(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                    }
                    options.noreturn = 1;
                } else if (tok.ident().name == "nostack") {
                    if (options.nostack) {
                        ERROR(lex.pointSpan(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                    }
                    options.nostack = 1;
                } else if (tok.ident().name == "att_syntax") {
                    if (options.attSyntax) {
                        ERROR(lex.pointSpan(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                    }
                    // TODO: x86(-64) only
                    options.attSyntax = 1;
                } else if (tok.ident().name == "raw") {
                    if (options.raw) {
                        ERROR(lex.pointSpan(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                    }
                    options.raw = 1;
                } else {
                    ERROR(lex.pointSpan(), E0000, "Unknown asm option - " << tok.ident().name);
                }

                if (lex.lookahead(0) == TOK_PAREN_CLOSE) {
                    GET_TOK(tok, lex);
                    break;
                }
            } while (GET_TOK(tok, lex) == TOK_COMMA);
            CHECK_TOK(tok, TOK_PAREN_CLOSE);

            GET_TOK(tok, lex);
            continue;
        }

        if (lex.lookahead(0) == TOK_EQUAL) {
            GET_CHECK_TOK(tok, lex, TOK_EQUAL);
            bindingName = v;
            v = getTokIdentRword(lex);
        }

        ASTExprNodeAsm2::Param paramSpec;
        if (v == "const") {
            auto e = ParseExpr0(lex);
            paramSpec = ASTExprNodeAsm2::Param::make_Const(std::move(e));
        } else if (v == "sym") {
            auto p = ParsePath(lex, PATH_GENERIC_EXPR);
            paramSpec = ASTExprNodeAsm2::Param::make_Sym(std::move(p));
        } else if (v == "label") {
            auto e = ParseExpr0(lex);
            if (!cast<ASTExprNodeBlock>(e.get())) {
                ERROR(sp, E0000, "asm! label operand requires a block");
            }
            paramSpec = ASTExprNodeAsm2::Param::make_Label({std::move(e)});
        } else {
            AsmDirection dir;
            if (v == "inlateout") {
                dir = AsmDirection::InLateOut;
            } else if (v == "in") {
                dir = AsmDirection::In;
            } else if (v == "out") {
                dir = AsmDirection::Out;
            } else if (v == "lateout") {
                dir = AsmDirection::LateOut;
            } else if (v == "inout") {
                dir = AsmDirection::InOut;
            } else {
                ERROR(sp, E0000, "Unknown asm fragment - `" << v << "`");
            }

            GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
            GET_TOK(tok, lex);
            AsmRegisterSpec regSpec;
            if (tok.type() == TOK_IDENT) {
                regSpec = AsmRegisterSpec::make_Class(getRegClass(wb, lex.pointSpan(), tok.ident().name));
            } else if (tok.type() == TOK_STRING) {
                regSpec = AsmRegisterSpec::make_Explicit(tok.str());
            } else {
                parseErrorUnexpected(lex, tok, {TOK_IDENT, TOK_STRING});
            }
            GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);

            if (lex.lookahead(0) == TOK_UNDERSCORE) {
                GET_TOK(tok, lex);
                switch (dir) {
                    case AsmDirection::LateOut:
                    case AsmDirection::Out:
                        break;
                    default:
                        ERROR(sp, E0000, "Invalid use of _ in asm!");
                }
                paramSpec = ASTExprNodeAsm2::Param::make_Reg({dir, std::move(regSpec), nullptr, nullptr});
            } else {
                auto e = ParseExpr0(lex);

                if (lex.lookahead(0) == TOK_FATARROW) {
                    switch (dir) {
                        case AsmDirection::InLateOut:
                        case AsmDirection::InOut:
                            break;
                        default:
                            ERROR(sp, E0000, "Invalid use of => in asm!");
                    }
                    GET_TOK(tok, lex);
                    if (lex.lookahead(0) == TOK_UNDERSCORE) {
                        GET_TOK(tok, lex);
                        paramSpec = ASTExprNodeAsm2::Param::make_Reg({dir, std::move(regSpec), mv$(e), nullptr});
                    } else {
                        auto e2 = ParseExpr0(lex);
                        paramSpec = ASTExprNodeAsm2::Param::make_Reg({dir, std::move(regSpec), mv$(e), mv$(e2)});
                    }
                } else {
                    paramSpec = ASTExprNodeAsm2::Param::make_RegSingle({dir, std::move(regSpec), mv$(e)});
                }
            }
        }

        names.push_back(bindingName);
        params.push_back(std::move(paramSpec));

        GET_TOK(tok, lex);
    }
    CHECK_TOK(tok, TOK_EOF);

    {
        bool seenNonPositional = false;
        for (size_t i = 0; i < params.size(); i++) {
            const AsmRegisterSpec* spec = nullptr;
            if (const auto* e = params[i].opt_Reg()) {
                spec = &e->spec;
            } else if (const auto* e = params[i].opt_RegSingle()) {
                spec = &e->spec;
            }
            const bool positional = (names[i] == RcString()) && !(spec && spec->is_Explicit());
            if (!positional) {
                seenNonPositional = true;
            } else if (seenNonPositional) {
                ERROR(sp, E0000, "positional arguments cannot follow named arguments or explicit register arguments");
            }
        }
    }

    {
        const auto& arch = TargetGetCurSpec(wb).arch.name;
        const bool isX86 = arch == "x86" || arch == "x86_64";
        const bool is64Bit = arch == "x86_64";
        std::map<std::string, std::string> seen;
        for (const auto& param : params) {
            const AsmRegisterSpec* spec = nullptr;
            if (const auto* e = param.opt_Reg()) {
                spec = &e->spec;
            } else if (const auto* e = param.opt_RegSingle()) {
                spec = &e->spec;
            }
            if (!spec || !spec->is_Explicit()) {
                continue;
            }
            const auto& name = spec->as_Explicit();
            if (isX86) {
                if (const char* what = x86ReservedRegister(name)) {
                    ERROR(sp, E0000, "invalid register `" << name << "`: " << what << " cannot be used as an operand for inline asm");
                }
            }
            const auto canonical = isX86 ? canonicalX86Register(name, is64Bit) : name;
            auto inserted = seen.insert(std::make_pair(canonical, name));
            if (!inserted.second) {
                ERROR(sp, E0000, "register `" << name << "` conflicts with register `" << inserted.first->second << "`");
            }
        }
    }

    bool hasLabel = false;
    bool hasOutputValue = false;
    for (const auto& param : params) {
        if (param.is_Label()) {
            hasLabel = true;
        } else if (const auto* reg = param.opt_RegSingle()) {
            hasOutputValue |= reg->dir != AsmDirection::In;
        } else if (const auto* reg = param.opt_Reg()) {
            hasOutputValue |= bool(reg->valOut);
        }
    }
    if (hasLabel && hasOutputValue) {
        ERROR(sp, E0000, "using both label and output operands for inline assembly is unstable in Rust 1.90");
    }

    if (!clobberAbis.empty()) {
        const auto& arch = TargetGetCurSpec(wb).arch.name;
        const bool isX86 = arch == "x86" || arch == "x86_64";
        const bool is64Bit = arch == "x86_64";
        std::set<std::string> explicitOutputs;
        for (const auto& param : params) {
            const AsmRegisterSpec* spec = nullptr;
            if (const auto* e = param.opt_Reg()) {
                if (e->dir != AsmDirection::In) {
                    spec = &e->spec;
                }
            } else if (const auto* e = param.opt_RegSingle()) {
                if (e->dir != AsmDirection::In) {
                    spec = &e->spec;
                }
            }
            if (spec && spec->is_Explicit()) {
                explicitOutputs.insert(isX86 ? canonicalX86Register(spec->as_Explicit(), is64Bit) : spec->as_Explicit());
            } else if (spec) {
                ERROR(sp, E0000, "asm with `clobber_abi` must specify explicit registers for outputs");
            }
        }

        std::set<std::string> added;
        for (const auto& abi : clobberAbis) {
            for (auto reg : getClobberAbiRegisters(wb, sp, abi)) {
                const auto canonical = isX86 ? canonicalX86Register(reg, is64Bit) : reg;
                if (explicitOutputs.count(canonical) || !added.insert(canonical).second) {
                    continue;
                }
                names.push_back({});
                params.push_back(ASTExprNodeAsm2::Param::make_Reg({AsmDirection::LateOut, AsmRegisterSpec::make_Explicit(mv$(reg)), nullptr, nullptr}));
            }
        }
    }

    if (options.nomem && options.readonly) {
        ERROR(sp, E0000, "asm! options `nomem` and `readonly` are mutually exclusive");
    }
    if (options.pure && !(options.nomem || options.readonly)) {
        ERROR(sp, E0000, "asm! marked `pure` without `nomem` or `readonly`");
    }
    if (options.noreturn && hasOutputValue) {
        ERROR(sp, E0000, "asm outputs are not allowed with the `noreturn` option");
    }

    unsigned nextIndex = 0;
    std::vector<AsmLine> lines;
    for (const auto& e : rawLines) {
        const auto& sp = e.first;
        const auto& text = e.second;

        AsmLine line;

        const char* c = text.c_str();
        std::string curString;
        while (*c) {
            if (*c == '}') {
                c++;
                if (!*c) {
                    ERROR(sp, E0000, "Unexpected EOF in asm! format string");
                }
                if (*c != '}') {
                    ERROR(sp, E0000, "Closing braces in `asm!` need to be written as `}}`");
                }
                c++;
                curString += '}';
                continue;
            }

            if (*c == '{') {
                c++;
                if (*c == '{') {
                    curString += '{';
                    c++;
                    continue;
                }

                std::string name;
                while (*c && *c != ':' && *c != '}') {
                    name += *c;
                    c++;
                }
                if (!*c) {
                    ERROR(sp, E0000, "Unexpected EOF in asm! format string");
                }
                AsmLineFragment frag;
                if (name.empty()) {
                    frag.index = nextIndex;
                    if (frag.index >= params.size()) {
                        ERROR(sp, E0000, "asm! format doesn't have enough arguments");
                    }
                    nextIndex++;
                } else if (std::isdigit(name[0])) {
                    frag.index = std::stoul(name);
                    if (frag.index >= params.size()) {
                        ERROR(sp, E0000, "asm! format string index out of range - " << frag.index);
                    }
                } else {
                    auto it = std::find(names.begin(), names.end(), name);
                    if (it == names.end()) {
                        ERROR(sp, E0000, "asm! format string references undefined value - `" << name << "`");
                    }
                    frag.index = it - names.begin();
                }
                assert(*c == ':' || *c == '}');
                if (*c == ':') {
                    c++;
                    if (!*c) {
                        ERROR(sp, E0000, "Unexpected EOF in asm! format string");
                    }
                    if (*c != '}') {
                        frag.modifier = *c;
                        c++;
                    }
                }
                if (!*c) {
                    ERROR(sp, E0000, "Unexpected EOF in asm! format string");
                }
                if (*c != '}') {
                    ERROR(sp, E0000, "Expected '}' in asm! format string");
                }

                frag.before = std::move(curString);
                curString.clear();
                line.frags.push_back(std::move(frag));
            } else {
                curString += *c;
            }
            c++;
        }
        line.trailing = std::move(curString);
        lines.push_back(std::move(line));
    }

    {
        std::set<unsigned> referenced;
        for (const auto& line : lines) {
            for (const auto& frag : line.frags) {
                referenced.insert(frag.index);
            }
        }
        unsigned unused = 0;
        for (size_t i = 0; i < params.size(); i++) {
            if (referenced.count(static_cast<unsigned>(i))) {
                continue;
            }
            const AsmRegisterSpec* spec = nullptr;
            if (const auto* e = params[i].opt_Reg()) {
                spec = &e->spec;
            } else if (const auto* e = params[i].opt_RegSingle()) {
                spec = &e->spec;
            }
            if (spec && spec->is_Explicit()) {
                continue;
            }
            if (params[i].is_Label()) {
                continue;
            }
            unused += 1;
        }
        if (unused == 1) {
            ERROR(sp, E0000, "unused asm argument");
        } else if (unused > 1) {
            ERROR(sp, E0000, "multiple unused asm arguments");
        }
    }

    for (const auto& line : lines) {
        for (const auto& frag : line.frags) {
            if (frag.index == UINT_MAX) {
                ERROR(sp, E0000, "asm! marked `pure` without `nomem` or `readonly`");
            }
            if (frag.modifier != '\0') {
                // TODO: Check that the modifier is valid for the specifier
            }
        }
    }

    ASTExprNodeP rv = ASTExprNodeP(new ASTExprNodeAsm2{mv$(options), mv$(lines), mv$(params)});
    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(InterpolatedFragment(InterpolatedFragment::EXPR, rv.release())))));
}

auto CGlobalAsmExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    auto o = CAsmExpander().expand(sp, wb, crate, tt, mod);

    auto node = o->getToken().takeFragNode();
    auto* nodeAp = cast<ASTExprNodeAsm2>(node.get());
    ASSERT_BUG(sp, nodeAp, "");
    auto& nodeA = *nodeAp;

    {
        const auto& o = nodeA.options;
        const char* bad = o.pure ? "pure" : o.nomem ? "nomem" : o.readonly ? "readonly" : o.preservesFlags ? "preserves_flags" : o.noreturn ? "noreturn" : o.nostack ? "nostack" : nullptr;
        if (bad) {
            ERROR(sp, E0000, "the `" << bad << "` option cannot be used with `global_asm!`");
        }
    }

    auto globalAsm = ASTGlobalAsm{std::move(nodeA.lines), {}, nodeA.options};
    globalAsm.operands.reserve(nodeA.params.size());
    for (auto& param : nodeA.params) {
        switch (param.tag()) {
            case ASTAsmParam::TAG_Const: {
                auto& expr = param.as_Const();
                globalAsm.operands.push_back(ASTGlobalAsm::Operand::make_Const(std::move(expr)));
                break;
            }
            case ASTAsmParam::TAG_Sym: {
                auto& path = param.as_Sym();
                globalAsm.operands.push_back(ASTGlobalAsm::Operand::make_Sym(std::move(path)));
                break;
            }
            case ASTAsmParam::TAG_Label: {
                ERROR(sp, E0000, "`label` is not allowed in `global_asm!`");
                break;
            }
            case ASTAsmParam::TAG_RegSingle: {
                ERROR(sp, E0000, "Only `sym` and `const` are allowed in `global_asm!`");
                break;
            }
            case ASTAsmParam::TAG_Reg: {
                ERROR(sp, E0000, "Only `sym` and `const` are allowed in `global_asm!`");
                break;
            }
        }
    }
    auto namedItem = ASTNamed<ASTItem>(sp, {}, ASTVisibility::makeBarePrivate(), "", ASTItem(std::move(globalAsm)));
    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(Token::TagTakeIP(), InterpolatedFragment(std::move(namedItem))))));
}

auto CNakedAsmExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    auto o = CAsmExpander().expand(sp, wb, crate, tt, mod);

    auto node = o->getToken().takeFragNode();
    auto* nodeAp = cast<ASTExprNodeAsm2>(node.get());
    ASSERT_BUG(sp, nodeAp, "");
    nodeAp->options.naked = true;

    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(InterpolatedFragment(InterpolatedFragment::EXPR, node.release())))));
}

GenericAssertCaptureVisitor::GenericAssertCaptureVisitor(RcString coreCrate, Ident::Hygiene hygiene)
    : coreCrate(coreCrate)
    , hygiene(hygiene)
{
}

auto GenericAssertCaptureVisitor::manage(ASTExprNodeP& node) -> void {
    if (!node) {
        return;
    }
    auto* previous = current;
    current = &node;
    node->visit(*this);
    current = previous;
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeArray& node) -> void {
    manage(node.size);
    for (auto& value : node.values) {
        manage(value);
    }
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeBinOp& node) -> void {
    const bool wasConsumed = consumed;
    switch (node.type) {
        case ASTExprNodeBinOp::CMPEQU:
        case ASTExprNodeBinOp::CMPNEQU:
        case ASTExprNodeBinOp::CMPLT:
        case ASTExprNodeBinOp::CMPLTE:
        case ASTExprNodeBinOp::CMPGT:
        case ASTExprNodeBinOp::CMPGTE:
        case ASTExprNodeBinOp::RANGE:
        case ASTExprNodeBinOp::RANGE_INC:
            consumed = false;
            break;
        default:
            consumed = true;
            break;
    }
    manage(node.left);
    manage(node.right);
    consumed = wasConsumed;
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeCallPath& node) -> void {
    for (auto& arg : node.args) {
        manage(arg);
    }
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeCallMethod& node) -> void {
    manage(node.val);
    for (auto& arg : node.args) {
        manage(arg);
    }
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeCallObject& node) -> void {
    for (auto& arg : node.args) {
        manage(arg);
    }
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeCast& node) -> void {
    manage(node.value);
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeDeref& node) -> void {
    const bool wasConsumed = consumed;
    consumed = false;
    manage(node.value);
    consumed = wasConsumed;
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeIf& node) -> void {
    for (auto& arm : node.arms) {
        for (auto& condition : arm.conditions) {
            manage(condition.value);
        }
    }
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeIndex& node) -> void {
    manage(node.obj);
    manage(node.idx);
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeLetBinding& node) -> void {
    manage(node.value);
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeMatch& node) -> void {
    manage(node.val);
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeUniOp& node) -> void {
    const bool wasConsumed = consumed;
    consumed = node.type != ASTExprNodeUniOp::REF;
    manage(node.value);
    consumed = wasConsumed;
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeNamedValue& node) -> void {
    if (!node.path.isTrivial()) {
        return;
    }
    const auto& name = node.path.asTrivial();
    for (const auto& capture : captures) {
        if (capture.path == node.path) {
            return;
        }
    }

    const auto captureIndex = captures.size();
    const auto captureName = RcString::newInterned(FMT("__capture" << captureIndex));
    const auto localBindName = RcString::newInterned(FMT("__local_bind" << captureIndex));
    captures.push_back({ASTPath(node.path), name, captureName, localBindName, !consumed});

    if (consumed) {
        ASTExprNodeBlock captureBlock;
        captureBlock.setSpan(node.span());
        captureBlock.pushStmt(makeTryCapture(captureName, localBindName, node.span()));
        captureBlock.pushTailExpr(makeGeneratedValue(localBindName, node.span()));
        *current = ASTExprNodeP(box$(ASTExprNodeDeref(ASTExprNodeP(box$(captureBlock)))));
    } else {
        *current = ASTExprNodeP(box$(ASTExprNodeDeref(makeGeneratedValue(localBindName, node.span()))));
    }
    (*current)->setSpan(node.span());
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeStructLiteral& node) -> void {
    for (auto& value : node.values) {
        manage(value.value);
    }
    manage(node.baseValue);
}

auto GenericAssertCaptureVisitor::visit(ASTExprNodeTuple& node) -> void {
    for (auto& value : node.values) {
        manage(value);
    }
}

auto GenericAssertCaptureVisitor::makeTryCapture(RcString captureName, RcString localBindName, const Span& sp) const -> ASTExprNodeP {
    auto wrapper = ASTExprNodeP(box$(ASTExprNodeCallPath(ASTPath(ASTAbsolutePath(coreCrate, {RcString::newInterned("asserting"), RcString::newInterned("Wrapper")})), makeVec1(makeGeneratedValue(localBindName, sp)))));
    wrapper->setSpan(sp);

    auto wrapperRef = ASTExprNodeP(box$(ASTExprNodeUniOp(ASTExprNodeUniOp::REF, ASTExprNodeP(wrapper.release()))));
    wrapperRef->setSpan(sp);
    auto captureRef = ASTExprNodeP(box$(ASTExprNodeUniOp(ASTExprNodeUniOp::REFMUT, makeGeneratedValue(captureName, sp))));
    captureRef->setSpan(sp);
    auto call = ASTExprNodeP(box$(ASTExprNodeCallMethod(ASTExprNodeP(wrapperRef.release()), ASTPathNode(hygiene, RcString::newInterned("try_capture")), makeVec1(ASTExprNodeP(captureRef.release())))));
    call->setSpan(sp);
    return call;
}

auto GenericAssertCaptureVisitor::generatedPath(RcString name) const -> ASTPath {
    return ASTPath::newRelative(hygiene, {ASTPathNode(hygiene, name)});
}

auto GenericAssertCaptureVisitor::makeGeneratedValue(RcString name, const Span& sp) const -> ASTExprNodeP {
    auto value = ASTExprNodeP(box$(ASTExprNodeNamedValue(generatedPath(name))));
    value->setSpan(sp);
    return value;
}

auto CExpanderAssert::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;

    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;
    lex.parseState().module = &mod;

    auto n = ParseExpr0(lex);
    ASSERT_BUG(sp, n, "No expression returned");

    std::vector<TokenTree> toks;
    const auto expansionHygiene = Ident::Hygiene::newScope(wb.id, *wb.pool);

    bool closeOuterBlock = false;

    GET_TOK(tok, lex);
    if (tok == TOK_COMMA && lex.lookahead(0) == TOK_EOF) {
        GET_TOK(tok, lex);
    }
    if (tok == TOK_COMMA) {
        toks.push_back(Token(TOK_RWORD_IF));
        toks.push_back(Token(TOK_EXCLAM));
        toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())));
        toks.push_back(Token(TOK_BRACE_OPEN));
        toks.push_back(Token(TOK_IDENT, RcString::newInterned("panic")));
        toks.push_back(Token(TOK_EXCLAM));
        toks.push_back(Token(TOK_PAREN_OPEN));

        auto fmt = ParseExpr0(lex);
        if (lex.getTokenIf(TOK_COMMA)) {
            toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, fmt.release())));

            while (lex.lookahead(0) != TOK_EOF) {
                toks.push_back(TOK_COMMA);

                if ((lex.lookahead(0) == TOK_IDENT || Token::typeIsRword(lex.lookahead(0))) && lex.lookahead(1) == TOK_EQUAL) {
                    toks.push_back(lex.getToken());
                    toks.push_back(lex.getToken());
                    toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, ParseExpr0(lex).release())));
                } else {
                    toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, ParseExpr0(lex).release())));
                }
                if (lex.lookahead(0) != TOK_COMMA) {
                    break;
                }
                GET_CHECK_TOK(tok, lex, TOK_COMMA);
            }
        } else {
            toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, fmt.release())));
        }

        GET_CHECK_TOK(tok, lex, TOK_EOF);
        toks.push_back(Token(TOK_PAREN_CLOSE));
    } else if (tok == TOK_EOF) {
        std::stringstream ss;
        n->print(ss);
        auto conditionText = ss.str();

        const auto genericAssert = RcString::newInterned("generic_assert");
        if (crate.features.count(genericAssert) != 0) {
            if (n->nodeKind() == ASTExprNodeBinOp::kind && conditionText.size() >= 2) {
                conditionText.erase(conditionText.begin());
                conditionText.pop_back();
            }
            GenericAssertCaptureVisitor captureVisitor(crate.extCratenameCore, expansionHygiene);
            captureVisitor.manage(n);

            toks.push_back(Token(TOK_BRACE_OPEN));
            closeOuterBlock = true;

            toks.push_back(Token(TOK_RWORD_USE));
            toks.push_back(Token(Token::TagTakeIP(), InterpolatedFragment(ASTPath(ASTAbsolutePath(crate.extCratenameCore, {RcString::newInterned("asserting"), RcString::newInterned("TryCaptureGeneric")})))));
            toks.push_back(Token(TOK_SEMICOLON));
            toks.push_back(Token(TOK_RWORD_USE));
            toks.push_back(Token(Token::TagTakeIP(), InterpolatedFragment(ASTPath(ASTAbsolutePath(crate.extCratenameCore, {RcString::newInterned("asserting"), RcString::newInterned("TryCapturePrintable")})))));
            toks.push_back(Token(TOK_SEMICOLON));

            for (size_t i = 0; i < captureVisitor.captures.size(); i++) {
                const auto& capture = captureVisitor.captures[i];
                toks.push_back(Token(TOK_RWORD_LET));
                toks.push_back(Token(TOK_RWORD_MUT));
                toks.push_back(Token(TOK_IDENT, capture.captureName));
                toks.push_back(Token(TOK_EQUAL));
                toks.push_back(Token(Token::TagTakeIP(), InterpolatedFragment(ASTPath(ASTAbsolutePath(crate.extCratenameCore, {RcString::newInterned("asserting"), RcString::newInterned("Capture"), RcString::newInterned("new")})))));
                toks.push_back(Token(TOK_PAREN_OPEN));
                toks.push_back(Token(TOK_PAREN_CLOSE));
                toks.push_back(Token(TOK_SEMICOLON));

                toks.push_back(Token(TOK_RWORD_LET));
                toks.push_back(Token(TOK_IDENT, capture.localBindName));
                toks.push_back(Token(TOK_EQUAL));
                toks.push_back(Token(TOK_AMP));
                toks.push_back(Token(Token::TagTakeIP(), InterpolatedFragment(ASTPath(capture.path), sp)));
                toks.push_back(Token(TOK_SEMICOLON));
            }

            toks.push_back(Token(TOK_RWORD_IF));
            toks.push_back(Token(TOK_EXCLAM));
            toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())));
            toks.push_back(Token(TOK_BRACE_OPEN));

            for (size_t i = 0; i < captureVisitor.captures.size(); i++) {
                const auto& capture = captureVisitor.captures[i];
                if (!capture.deferred) {
                    continue;
                }
                toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, captureVisitor.makeTryCapture(capture.captureName, capture.localBindName, sp).release())));
                toks.push_back(Token(TOK_SEMICOLON));
            }

            for (size_t i = 0; i < conditionText.size(); i++) {
                if (conditionText[i] == '{' || conditionText[i] == '}') {
                    conditionText.insert(conditionText.begin() + i, conditionText[i]);
                    i += 1;
                }
            }
            auto message = FMT("Assertion failed: " << conditionText);
            if (!captureVisitor.captures.empty()) {
                message += "\nWith captures:\n";
                for (const auto& capture : captureVisitor.captures) {
                    message += FMT("  " << capture.name << " = {:?}\n");
                }
            }
            toks.push_back(Token(TOK_IDENT, RcString::newInterned("panic")));
            toks.push_back(Token(TOK_EXCLAM));
            toks.push_back(Token(TOK_PAREN_OPEN));
            toks.push_back(Token(TOK_STRING, message, {}));
            for (size_t i = 0; i < captureVisitor.captures.size(); i++) {
                toks.push_back(Token(TOK_COMMA));
                toks.push_back(Token(TOK_IDENT, captureVisitor.captures[i].captureName));
            }
            toks.push_back(Token(TOK_PAREN_CLOSE));
        } else {
            toks.push_back(Token(TOK_RWORD_IF));
            toks.push_back(Token(TOK_EXCLAM));
            toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())));
            toks.push_back(Token(TOK_BRACE_OPEN));
            toks.push_back(Token(TOK_IDENT, RcString::newInterned("panic")));
            toks.push_back(Token(TOK_EXCLAM));
            toks.push_back(Token(TOK_PAREN_OPEN));
            toks.push_back(Token(TOK_STRING, std::string("assertion failed: {}"), {}));
            toks.push_back(Token(TOK_COMMA));
            toks.push_back(Token(TOK_STRING, ss.str(), {}));
            toks.push_back(Token(TOK_PAREN_CLOSE));
        }
    } else {
        parseErrorUnexpected(lex, tok, {TOK_COMMA, TOK_EOF});
    }

    toks.push_back(Token(TOK_BRACE_CLOSE));
    if (closeOuterBlock) {
        toks.push_back(Token(TOK_BRACE_CLOSE));
    }

    return box$(TTStreamO(sp, ParseState(), TokenTree(ASTEdition::Rust2015, expansionHygiene, mv$(toks))));
}

auto CExpanderCompileError::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    ERROR(sp, E0000, "compile_error! " << tt);
}

auto CConcatExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;

    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;

    std::string rv;
    do {
        if (LOOK_AHEAD(lex) == TOK_EOF) {
            GET_TOK(tok, lex);
            break;
        }

        auto v = ParseExpr0(lex);
        ExpandBareExpr(wb, crate, mod, v);
        // TODO: Visitor instead
        if (auto* vp = cast<ASTExprNodeString>(v.get())) {
            rv += vp->value;
        } else if (auto* vp = cast<ASTExprNodeInteger>(v.get())) {
            if (vp->datatype == CORETYPE_CHAR) {
                rv += Codepoint{static_cast<u32>(vp->value.truncateU64())};
            } else {
                rv += FMT(vp->value);
            }
        } else if (auto* vp = cast<ASTExprNodeFloat>(v.get())) {
            rv += formatFloatValueForToken(vp->value);
        } else if (auto* vp = cast<ASTExprNodeBool>(v.get())) {
            rv += (vp->value ? "true" : "false");
        } else if (auto* vp = cast<ASTExprNodeUniOp>(v.get())) {
            const auto* inner = vp->value.get();
            if (vp->type != ASTExprNodeUniOp::NEGATE) {
                ERROR(sp, E0000, "Unexpected expression type in concat! argument");
            } else if (const auto* iv = cast<const ASTExprNodeInteger>(inner)) {
                rv += FMT("-" << iv->value);
            } else if (const auto* fv = cast<const ASTExprNodeFloat>(inner)) {
                rv += "-";
                rv += formatFloatValueForToken(fv->value);
            } else {
                ERROR(sp, E0000, "Unexpected expression type in concat! argument");
            }
        } else {
            ERROR(sp, E0000, "Unexpected expression type in concat! argument");
        }
    } while (GET_TOK(tok, lex) == TOK_COMMA);
    if (tok.type() != TOK_EOF) {
        parseErrorUnexpected(lex, tok, {TOK_COMMA, TOK_EOF});
    }

    return box$(TTStreamO(sp, ParseState(), TokenTree(tt.getEdition(), Token(TOK_STRING, mv$(rv), {}))));
}

auto CConcatBytesExpander::getArrayByte(const Span& sp, const ASTExprNode& node) -> char {
    const auto* value = cast<const ASTExprNodeInteger>(&node);
    if (!value || (value->datatype != CORETYPE_ANY && value->datatype != CORETYPE_U8) || !value->value.isU64() || value->value.truncateU64() > 0xff) {
        ERROR(sp, E0000, "concat_bytes! array elements must be byte or u8 literals");
    }
    return static_cast<char>(value->value.truncateU64());
}

auto CConcatBytesExpander::append(const Span& sp, std::string& output, const ASTExprNode& node) -> void {
    if (const auto* value = cast<const ASTExprNodeInteger>(&node)) {
        if (value->datatype != CORETYPE_U8 || !value->value.isU64() || value->value.truncateU64() > 0xff) {
            ERROR(sp, E0000, "concat_bytes! arguments must be byte string, byte, or byte-array literals");
        }
        output.push_back(static_cast<char>(value->value.truncateU64()));
        return;
    }
    if (const auto* value = cast<const ASTExprNodeByteString>(&node)) {
        output += value->value;
        return;
    }
    if (const auto* value = cast<const ASTExprNodeArray>(&node)) {
        if (!value->size) {
            for (const auto& element : value->values) {
                output.push_back(getArrayByte(sp, *element));
            }
            return;
        }

        const auto* count = cast<const ASTExprNodeInteger>(value->size.get());
        if (!count || !count->value.isU64()) {
            ERROR(sp, E0000, "concat_bytes! repeat count must be an integer literal");
        }
        const auto byte = getArrayByte(sp, *value->values.at(0));
        output.append(static_cast<size_t>(count->value.truncateU64()), byte);
        return;
    }
    ERROR(sp, E0000, "concat_bytes! arguments must be byte string, byte, or byte-array literals");
}

auto CConcatBytesExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;
    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;

    std::string output;
    do {
        if (LOOK_AHEAD(lex) == TOK_EOF) {
            GET_TOK(tok, lex);
            break;
        }

        auto value = ParseExpr0(lex);
        ExpandBareExpr(wb, crate, mod, value);
        append(sp, output, *value);
    } while (GET_TOK(tok, lex) == TOK_COMMA);
    if (tok.type() != TOK_EOF) {
        parseErrorUnexpected(lex, tok, {TOK_COMMA, TOK_EOF});
    }

    return box$(TTStreamO(sp, ParseState(), TokenTree(tt.getEdition(), Token(TOK_BYTESTRING, mv$(output), {}))));
}

auto CConcatIdentsExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;
    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;

    std::string rv;

    do {
        if (LOOK_AHEAD(lex) == TOK_EOF) {
            GET_TOK(tok, lex);
            break;
        }

        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        rv += tok.ident().name.c_str();
    } while (GET_TOK(tok, lex) == TOK_COMMA);
    if (tok.type() != TOK_EOF) {
        parseErrorUnexpected(lex, tok, {TOK_COMMA, TOK_EOF});
    }

    return box$(TTStreamO(sp, ParseState(), TokenTree(tt.getEdition(), Token(TOK_IDENT, Ident(lex.getHygiene(), RcString::newInterned(rv))))));
}

auto CExpanderEnv::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    std::string varname = getString(sp, wb, crate, mod, tt);

    const char* varValCstr = getenv(varname.c_str());
    if (!varValCstr) {
        ERROR(sp, E0000, "Environment variable '" << varname << "' not defined");
    }
    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(TOK_STRING, std::string(varValCstr), {}))));
}

auto CExpanderOptionEnv::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    std::string varname = getString(sp, wb, crate, mod, tt);
    std::vector<TokenTree> rv;

    const char* varValCstr = getenv(varname.c_str());
    if (!varValCstr) {
        rv.reserve(7);
        rv.push_back(Token(TOK_IDENT, RcString::newInterned("None")));
        rv.push_back(Token(TOK_DOUBLE_COLON));
        rv.push_back(Token(TOK_LT));
        rv.push_back(Token(TOK_AMP));
        rv.push_back(Token(TOK_LIFETIME, RcString::newInterned("static")));
        rv.push_back(Token(TOK_IDENT, RcString::newInterned("str")));
        rv.push_back(Token(TOK_GT));
    } else {
        rv.reserve(4);
        rv.push_back(Token(TOK_IDENT, RcString::newInterned("Some")));
        rv.push_back(Token(TOK_PAREN_OPEN));
        rv.push_back(Token(TOK_STRING, std::string(varValCstr), {}));
        rv.push_back(Token(TOK_PAREN_CLOSE));
    }
    return box$(TTStreamO(sp, ParseState(), TokenTree(ASTEdition::Rust2015, {}, mv$(rv))));
}

auto CExpanderFile::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(TOK_STRING, std::string(SourceLocation(sp).filename.c_str()), {}))));
}

auto CExpanderLine::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(U128(SourceLocation(sp).line), CORETYPE_U32))));
}

auto CExpanderColumn::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(U128(SourceLocation(sp).column), CORETYPE_U32))));
}

auto CExpanderUnstableColumn::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(U128(SourceLocation(sp).column), CORETYPE_U32))));
}

auto CExpanderModulePath::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    std::string pathStr;
    if (LexFindReservedWord(crate.crateNameSet, crate.edition) != TOK_NULL) {
        pathStr += "r#";
    }
    pathStr += crate.crateNameSet;
    for (const auto& comp : mod.path().nodes) {
        if (comp.c_str()[0] == '#') {
            continue;
        }
        pathStr += "::";
        pathStr += comp.c_str();
    }
    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(TOK_STRING, mv$(pathStr), {}))));
}

auto FmtArgs::operator==(const FmtArgs& x) const -> bool {
    return std::memcmp(this, &x, sizeof(*this)) == 0;
}

auto FmtArgs::operator!=(const FmtArgs& x) const -> bool {
#define CMP(f)    \
    if (f != x.f) \
    return true
    CMP(align);
    CMP(alignChar);
    CMP(sign);
    CMP(alternate);
    CMP(zeroPad);
    CMP(debugTy);
    CMP(widthIsArg);
    CMP(width);
    CMP(precSet);
    CMP(precIsArg);
    CMP(prec);
    return false;
}

auto CFormatArgsExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;

    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;
    lex.parseState().module = &mod;

    return expandFormatArgs(sp, wb, crate, lex, /*add_newline=*/false);
}

auto CConstFormatArgsExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;

    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;
    lex.parseState().module = &mod;

    return expandFormatArgs(sp, wb, crate, lex, /*add_newline=*/false);
}

auto CFormatArgsNlExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;

    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;
    lex.parseState().module = &mod;

    return expandFormatArgs(sp, wb, crate, lex, /*add_newline=*/true);
}

auto CIncludeExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;
    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;

    auto path = includeGetString(sp, lex, crate, mod);
    GET_CHECK_TOK(tok, lex, TOK_EOF);

    std::string filePath = getPathRelativeTo(sp.getTopFileSpan().filename.c_str(), mv$(path));
    crate.extraFiles.push_back(filePath);

    ParseState ps;
    ps.module = &mod;
    return box$(Lexer(wb.id, *crate.hirPool, filePath, crate.edition, ps));
}

auto CIncludeBytesExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;
    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;

    auto path = includeGetString(sp, lex, crate, mod);
    GET_CHECK_TOK(tok, lex, TOK_EOF);

    std::string filePath = getPathRelativeTo(sp.getTopFileSpan().filename.c_str(), mv$(path));
    crate.extraFiles.push_back(filePath);

    std::ifstream is(filePath);
    if (!is.good()) {
        ERROR(sp, E0000, "Cannot open file " << filePath << " for include_bytes!");
    }
    std::stringstream ss;
    ss << is.rdbuf();

    std::vector<TokenTree> toks;
    toks.push_back(Token(TOK_BYTESTRING, mv$(ss.str()), {}));
    return box$(TTStreamO(sp, ParseState(), TokenTree(ASTEdition::Rust2015, Ident::Hygiene::newScope(wb.id, *wb.pool), mv$(toks))));
}

auto CIncludeStrExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;
    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;

    auto path = includeGetString(sp, lex, crate, mod);
    GET_CHECK_TOK(tok, lex, TOK_EOF);

    std::string filePath = getPathRelativeTo(sp.getTopFileSpan().filename.c_str(), mv$(path));
    crate.extraFiles.push_back(filePath);

    std::ifstream is(filePath);
    if (!is.good()) {
        ERROR(sp, E0000, "Cannot open file " << filePath << " for include_str!");
    }
    std::stringstream ss;
    ss << is.rdbuf();

    std::vector<TokenTree> toks;
    toks.push_back(Token(TOK_STRING, mv$(ss.str()), {}));
    return box$(TTStreamO(sp, ParseState(), TokenTree(ASTEdition::Rust2015, Ident::Hygiene::newScope(wb.id, *wb.pool), mv$(toks))));
}

auto CExpanderPanic::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;

    auto edition = crate.edition;
    if (tt.hygiene().hasModPath() && tt.hygiene().modPath().crate != "") {
        edition = crate.externCrates.at(tt.hygiene().modPath().crate).hir->edition;
    }
    std::vector<TokenTree> toks;
    toks.push_back(Token(TOK_DOUBLE_COLON));
    const auto& panicCrate = crate.extCratenameStd != "" ? crate.extCratenameStd : crate.extCratenameCore;
    toks.push_back(Token(TOK_STRING, std::string(panicCrate.c_str()), {}));
    toks.push_back(Token(TOK_DOUBLE_COLON));
    toks.push_back(Token(TOK_IDENT, RcString::newInterned("panic")));
    toks.push_back(Token(TOK_DOUBLE_COLON));
    switch (edition) {
        case ASTEdition::Rust2015:
        case ASTEdition::Rust2018:
            toks.push_back(Token(TOK_IDENT, RcString::newInterned("panic_2015")));
            break;
        case ASTEdition::Rust2021:
        case ASTEdition::Rust2024:
            toks.push_back(Token(TOK_IDENT, RcString::newInterned("panic_2021")));
            break;
    }
    toks.push_back(Token(TOK_EXCLAM));
    toks.push_back(Token(TOK_PAREN_OPEN));
    if (tt.size() > 0) {
        toks.push_back(tt.clone());
    }
    toks.push_back(Token(TOK_PAREN_CLOSE));

    return box$(TTStreamO(sp, ParseState(), TokenTree(edition, Ident::Hygiene::newScope(wb.id, *wb.pool), mv$(toks))));
}

auto CExpanderUnreachable::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;

    auto edition = crate.edition;
    if (tt.hygiene().hasModPath() && tt.hygiene().modPath().crate != "") {
        edition = crate.externCrates.at(tt.hygiene().modPath().crate).hir->edition;
    }
    std::vector<TokenTree> toks;
    toks.push_back(Token(TOK_DOUBLE_COLON));
    toks.push_back(Token(TOK_STRING, std::string(crate.extCratenameCore.c_str()), {}));
    toks.push_back(Token(TOK_DOUBLE_COLON));
    toks.push_back(Token(TOK_IDENT, RcString::newInterned("panic")));
    toks.push_back(Token(TOK_DOUBLE_COLON));
    switch (crate.edition) {
        case ASTEdition::Rust2015:
        case ASTEdition::Rust2018:
            toks.push_back(Token(TOK_IDENT, RcString::newInterned("unreachable_2015")));
            break;
        case ASTEdition::Rust2021:
        case ASTEdition::Rust2024:
            toks.push_back(Token(TOK_IDENT, RcString::newInterned("unreachable_2021")));
            break;
    }
    toks.push_back(Token(TOK_EXCLAM));
    toks.push_back(Token(TOK_PAREN_OPEN));
    if (tt.size() > 0) {
        toks.push_back(tt.clone());
    }
    toks.push_back(Token(TOK_PAREN_CLOSE));

    return box$(TTStreamO(sp, ParseState(), TokenTree(edition, Ident::Hygiene::newScope(wb.id, *wb.pool), mv$(toks))));
}

auto CExpanderRegisterDiagnostic::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    return box$(TTStreamO(sp, ParseState(), TokenTree()));
}

auto CExpanderDiagnosticUsed::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    return box$(TTStreamO(sp, ParseState(), TokenTree()));
}

auto CExpanderBuildDiagnosticArray::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;

    Token tok;

    GET_CHECK_TOK(tok, lex, TOK_IDENT);
    GET_CHECK_TOK(tok, lex, TOK_COMMA);
    GET_CHECK_TOK(tok, lex, TOK_IDENT);
    auto itemName = tok.ident();
    GET_CHECK_TOK(tok, lex, TOK_EOF);

    std::vector<TokenTree> toks;
    toks.push_back(TOK_RWORD_PUB);
    toks.push_back(TOK_RWORD_STATIC);
    toks.push_back(Token(TOK_IDENT, itemName));
    toks.push_back(TOK_COLON);
    toks.push_back(TOK_SQUARE_OPEN);
    toks.push_back(TOK_PAREN_OPEN);
    toks.push_back(TOK_AMP);
    toks.push_back(Token(TOK_LIFETIME, RcString::newInterned("static")));
    toks.push_back(Token(TOK_IDENT, RcString::newInterned("str")));
    toks.push_back(TOK_COMMA);
    toks.push_back(TOK_AMP);
    toks.push_back(Token(TOK_LIFETIME, RcString::newInterned("static")));
    toks.push_back(Token(TOK_IDENT, RcString::newInterned("str")));
    toks.push_back(TOK_PAREN_CLOSE);
    toks.push_back(TOK_SEMICOLON);
    toks.push_back(Token(U128(0), CORETYPE_UINT));
    toks.push_back(TOK_SQUARE_CLOSE);
    toks.push_back(TOK_EQUAL);
    toks.push_back(TOK_SQUARE_OPEN);
    toks.push_back(TOK_SQUARE_CLOSE);
    toks.push_back(TOK_SEMICOLON);

    return box$(TTStreamO(sp, ParseState(), TokenTree(ASTEdition::Rust2015, lex.getHygiene(), mv$(toks))));
}

auto CExpander::expand(const Span& sp, const WireBoard& wb, const ASTCrate& crate, const TokenTree& tt, ASTModule& mod) -> std::unique_ptr<TokenStream> {
    Token tok;
    std::string rv;
    eTokenType prev = TOK_NULL;

    auto lex = TTStream(sp, ParseState(), tt);
    lex.parseState().wb = &wb;
    while (GET_TOK(tok, lex) != TOK_EOF) {
        if (!rv.empty() && tokensNeedSpace(prev, tok.type())) {
            rv += " ";
        }
        rv += tok.toStr();
        prev = tok.type();
    }

    return box$(TTStreamO(sp, ParseState(), TokenTree(Token(TOK_STRING, mv$(rv), {}))));
}
