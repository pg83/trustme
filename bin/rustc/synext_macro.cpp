#include "synext_macro.h"

#include "common.h"
#include "synext_macro.h"
#include "synext.h" // for Expand_BareExpr
#include "parse_tokentree.h"
#include "parse_ttstream.h"
#include "parse_common.h"
#include "parse_parseerror.h"
#include "ast_expr.h" // for ExprNode_*
#include "parse_interpolated_fragment.h"
#include "ast_crate.h"
#include "hir_asm.h"
#include "trans_target.h"
#include <cctype>
#include <string_view>
#include "expand_cfg.h"

namespace {
    ::std::string get_string(const Span& sp, TokenStream& lex, const ::AST::Crate& crate, AST::Module& mod) {
        auto n = Expand_ParseAndExpand_ExprVal(crate, mod, lex);

        auto* format_string_np = dynamic_cast<AST::ExprNode_String*>(&*n);
        if (!format_string_np) {
            ERROR(sp, E0000, "asm! requires a string literal - got " << *n);
        }
        //const auto& format_string_sp = format_string_np->span();
        return mv$(format_string_np->m_value);
    }

    RcString get_tok_ident_rword(TokenStream& lex) {
        Token tok;
        GET_TOK(tok, lex);
        if (tok.type() == TOK_IDENT) {
            return tok.ident().name;
        }
        if (Token::type_is_rword(tok.type())) {
            return tok.to_str().c_str();
        }
        throw ParseError::Unexpected(lex, tok, TOK_IDENT);
    }
}

class CLlvmAsmExpander: public ExpandProcMacro {
public:
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;
        auto lex = TTStream(sp, ParseState(), tt);

        auto template_text = get_string(sp, lex, crate, mod);
        ::std::vector<::AST::ExprNode_Asm::ValRef> outputs;
        ::std::vector<::AST::ExprNode_Asm::ValRef> inputs;
        ::std::vector<::std::string> clobbers;
        ::std::vector<::std::string> flags;

        // Outputs
        if (lex.lookahead(0) == TOK_DOUBLE_COLON) {
            GET_TOK(tok, lex);
            lex.putback(Token(TOK_COLON));
        } else if (lex.lookahead(0) == TOK_COLON) {
            GET_TOK(tok, lex);

            while (lex.lookahead(0) == TOK_STRING) {
                //auto name = get_string(sp, lex);
                GET_CHECK_TOK(tok, lex, TOK_STRING);
                auto name = mv$(tok.str());

                GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
                auto val = Parse_Expr0(lex);
                GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);

                outputs.push_back(::AST::ExprNode_Asm::ValRef{mv$(name), mv$(val)});

                if (lex.lookahead(0) != TOK_COMMA) {
                    break;
                }

                GET_TOK(tok, lex);
            }
        } else {
        }

        // Inputs
        if (lex.lookahead(0) == TOK_DOUBLE_COLON) {
            GET_TOK(tok, lex);
            lex.putback(Token(TOK_COLON));
        } else if (lex.lookahead(0) == TOK_COLON) {
            GET_TOK(tok, lex);

            while (lex.lookahead(0) == TOK_STRING) {
                GET_CHECK_TOK(tok, lex, TOK_STRING);
                auto name = mv$(tok.str());

                GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
                auto val = Parse_Expr0(lex);
                GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);

                inputs.push_back(::AST::ExprNode_Asm::ValRef{mv$(name), mv$(val)});

                if (lex.lookahead(0) != TOK_COMMA) {
                    break;
                }
                GET_TOK(tok, lex);
            }
        } else {
        }

        // Clobbers
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

        // Flags
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

        // has to be the end
        if (lex.lookahead(0) != TOK_EOF) {
            ERROR(sp, E0000, "Unexpected token in asm! - " << lex.getToken());
        }

        // Convert this into an AST node and insert as an intepolated expression
        ::AST::ExprNodeP rv = ::AST::ExprNodeP(new ::AST::ExprNode_Asm{mv$(template_text), mv$(outputs), mv$(inputs), mv$(clobbers), mv$(flags)});
        return box$(TTStreamO(sp, ParseState(), TokenTree(Token(InterpolatedFragment(InterpolatedFragment::EXPR, rv.release())))));
    }
};

namespace {
    AsmCommon::RegisterClass get_reg_class_x8664(const Span& sp, const RcString& str) {
        if (str == "reg") {
            return AsmCommon::RegisterClass::x86_reg;
        }
        if (str == "reg_abcd") {
            return AsmCommon::RegisterClass::x86_reg_abcd;
        }
        if (str == "reg_byte") {
            return AsmCommon::RegisterClass::x86_reg_byte;
        }
        if (str == "kreg") {
            return AsmCommon::RegisterClass::x86_kreg;
        }
        if (str == "xmm_reg") {
            return AsmCommon::RegisterClass::x86_xmm;
        }
        if (str == "ymm_reg") {
            return AsmCommon::RegisterClass::x86_ymm;
        }
        if (str == "zmm_reg") {
            return AsmCommon::RegisterClass::x86_zmm;
        }
        ERROR(sp, E0000, "Unknown register for x86/x86-64 - `" << str << "`");
    }

    AsmCommon::RegisterClass get_reg_class_riscv(const Span& sp, const RcString& str) {
        if (str == "reg") {
            return AsmCommon::RegisterClass::riscv_reg;
        }
        if (str == "freg") {
            return AsmCommon::RegisterClass::riscv_freg;
        }
        ERROR(sp, E0000, "Unknown register for riscv64 - `" << str << "`");
    }

    AsmCommon::RegisterClass get_reg_class(const Span& sp, const RcString& str) {
        if (Target_GetCurSpec().m_arch.m_name == "x86_64") {
            return get_reg_class_x8664(sp, str);
        }
        if (Target_GetCurSpec().m_arch.m_name == "x86") {
            return get_reg_class_x8664(sp, str);
        }
        if (Target_GetCurSpec().m_arch.m_name == "riscv64") {
            return get_reg_class_riscv(sp, str);
        }
        ERROR(sp, E0000, "Unknown architecture for asm!");
    }
}

class CAsmExpander: public ExpandProcMacro {
public:
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        // Stabilisation-path `asm!`

        Token tok;
        auto lex = TTStream(sp, ParseState(), tt);

        std::vector<std::pair<Span, std::string>> raw_lines;
        do {
            auto ps = lex.start_span();
            auto attrs = Parse_ItemAttrs(lex);
            auto text = get_string(sp, lex, crate, mod);
            auto sp = lex.end_span(ps);
            if (check_cfg_attrs(attrs)) {
                raw_lines.push_back(std::make_pair(sp, std::move(text)));
            }

            if (lex.lookahead(0) == TOK_EOF) {
                GET_TOK(tok, lex);
                break;
            }
            GET_CHECK_TOK(tok, lex, TOK_COMMA);
        } while (lex.lookahead(0) == TOK_STRING || lex.lookahead(0) == TOK_HASH);

        std::vector<AST::ExprNode_Asm2::Param> params;
        std::vector<RcString> names;
        AsmCommon::Options options;
        while (tok.type() == TOK_COMMA) {
            if (lex.lookahead(0) == TOK_EOF) {
                GET_TOK(tok, lex);
                break;
            }

            RcString binding_name;
            auto v = get_tok_ident_rword(lex);
            if (v == "options") {
                GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
                do {
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);

                    if (tok.ident().name == "pure") {
                        if (options.pure) {
                            ERROR(lex.point_span(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                        }
                        options.pure = 1;
                    } else if (tok.ident().name == "nomem") {
                        if (options.nomem) {
                            ERROR(lex.point_span(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                        }
                        options.nomem = 1;
                    } else if (tok.ident().name == "readonly") {
                        if (options.readonly) {
                            ERROR(lex.point_span(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                        }
                        options.readonly = 1;
                    } else if (tok.ident().name == "preserves_flags") {
                        if (options.preserves_flags) {
                            ERROR(lex.point_span(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                        }
                        options.preserves_flags = 1;
                    } else if (tok.ident().name == "noreturn") {
                        if (options.noreturn) {
                            ERROR(lex.point_span(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                        }
                        options.noreturn = 1;
                    } else if (tok.ident().name == "nostack") {
                        if (options.nostack) {
                            ERROR(lex.point_span(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                        }
                        options.nostack = 1;
                    } else if (tok.ident().name == "att_syntax") {
                        if (options.att_syntax) {
                            ERROR(lex.point_span(), E0000, "Duplicate specification of option `" << tok.ident().name << "`");
                        }
                        // TODO: x86(-64) only
                        options.att_syntax = 1;
                    } else {
                        ERROR(lex.point_span(), E0000, "Unknown asm option - " << tok.ident().name);
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
                binding_name = v;
                v = get_tok_ident_rword(lex);
            }

            AST::ExprNode_Asm2::Param param_spec;
            if (v == "const") {
                auto e = Parse_Expr0(lex);
                param_spec = AST::ExprNode_Asm2::Param::make_Const(std::move(e));
            } else if (v == "sym") {
                auto p = Parse_Path(lex, PATH_GENERIC_EXPR);
                param_spec = AST::ExprNode_Asm2::Param::make_Sym(std::move(p));
            } else {
                AsmCommon::Direction dir;
                if (v == "inlateout") {
                    dir = AsmCommon::Direction::InLateOut;
                } else if (v == "in") {
                    dir = AsmCommon::Direction::In;
                } else if (v == "out") {
                    dir = AsmCommon::Direction::Out;
                } else if (v == "lateout") {
                    dir = AsmCommon::Direction::LateOut;
                } else if (v == "inout") {
                    dir = AsmCommon::Direction::InOut;
                } else {
                    ERROR(sp, E0000, "Unknown asm fragment - `" << tok.ident().name << "`");
                }

                GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
                GET_TOK(tok, lex);
                AsmCommon::RegisterSpec reg_spec;
                if (tok.type() == TOK_IDENT) {
                    //Target_GetCurSpec().m_arch
                    reg_spec = AsmCommon::RegisterSpec::make_Class(get_reg_class(lex.point_span(), tok.ident().name));
                } else if (tok.type() == TOK_STRING) {
                    reg_spec = AsmCommon::RegisterSpec::make_Explicit(tok.str());
                } else {
                    throw ParseError::Unexpected(lex, tok, {TOK_IDENT, TOK_STRING});
                }
                GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);

                if (lex.lookahead(0) == TOK_UNDERSCORE) {
                    GET_TOK(tok, lex);
                    // out or lateout only
                    switch (dir) {
                        case AsmCommon::Direction::LateOut:
                        case AsmCommon::Direction::Out:
                            break;
                        default:
                            ERROR(sp, E0000, "Invalid use of _ in asm!");
                    }
                    param_spec = AST::ExprNode_Asm2::Param::make_Reg({dir, std::move(reg_spec), nullptr, nullptr});
                } else {
                    auto e = Parse_Expr0(lex);

                    if (lex.lookahead(0) == TOK_FATARROW) {
                        // inout or inlateout only
                        switch (dir) {
                            case AsmCommon::Direction::InLateOut:
                            case AsmCommon::Direction::InOut:
                                break;
                            default:
                                ERROR(sp, E0000, "Invalid use of => in asm!");
                        }
                        GET_TOK(tok, lex);
                        if (lex.lookahead(0) == TOK_UNDERSCORE) {
                            GET_TOK(tok, lex);
                            param_spec = AST::ExprNode_Asm2::Param::make_Reg({dir, std::move(reg_spec), mv$(e), nullptr});
                        } else {
                            auto e2 = Parse_Expr0(lex);
                            param_spec = AST::ExprNode_Asm2::Param::make_Reg({dir, std::move(reg_spec), mv$(e), mv$(e2)});
                        }
                    } else {
                        // Note: Different variant to handle `inout(reg) foo` without duplicating
                        param_spec = AST::ExprNode_Asm2::Param::make_RegSingle({dir, std::move(reg_spec), mv$(e)});
                    }
                }
            }

            names.push_back(binding_name);
            params.push_back(std::move(param_spec));

            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_EOF);

        // - Sanity-check options
        if (options.nomem && options.readonly) {
            ERROR(sp, E0000, "asm! options `nomem` and `readonly` are mutually exclusive");
        }
        if (options.pure && !(options.nomem || options.readonly)) {
            ERROR(sp, E0000, "asm! marked `pure` without `nomem` or `readonly`");
        }
        //if( options.pure && /* has no saved outputs */ ) {
        //}
        //if( options.noreturn && /* has outputs */ ) {
        //}

        unsigned next_index = 0;
        std::vector<AsmCommon::Line> lines;
        for (const auto& e : raw_lines) {
            const auto& sp = e.first;
            const auto& text = e.second;

            AsmCommon::Line line;

            const char* c = text.c_str();
            std::string cur_string;
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
                    cur_string += '}';
                    continue;
                }

                if (*c == '{') {
                    c++;
                    if (*c == '{') {
                        cur_string += '{';
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
                    AsmCommon::LineFragment frag;
                    if (name.empty()) {
                        frag.index = next_index;
                        if (frag.index >= params.size()) {
                            ERROR(sp, E0000, "asm! format doesn't have enough arguments");
                        }
                        next_index++;
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

                    frag.before = std::move(cur_string);
                    cur_string.clear();
                    line.frags.push_back(std::move(frag));
                } else {
                    cur_string += *c;
                }
                c++;
            }
            line.trailing = std::move(cur_string);
            lines.push_back(std::move(line));
        }

        // - Sanity-check register modifiers
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

        // Convert this into an AST node and insert as an intepolated expression
        ::AST::ExprNodeP rv = ::AST::ExprNodeP(new ::AST::ExprNode_Asm2{mv$(options), mv$(lines), mv$(params)});
        return box$(TTStreamO(sp, ParseState(), TokenTree(Token(InterpolatedFragment(InterpolatedFragment::EXPR, rv.release())))));
    }
};

class CGlobalAsmExpander: public ExpandProcMacro {
public:
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        auto o = CAsmExpander().expand(sp, crate, tt, mod);

        auto node = o->getToken().take_frag_node();
        auto* node_ap = dynamic_cast<AST::ExprNode_Asm2*>(node.get());
        ASSERT_BUG(sp, node_ap, "");
        auto& node_a = *node_ap;

        auto global_asm = AST::GlobalAsm{std::move(node_a.m_lines), {}, node_a.m_options};
        for (auto& param : node_a.m_params) {
            if (!(param.is_Sym() || param.is_Const())) {
                ERROR(sp, E0000, "Only `sym` and `const` are allowed in `global_asm!`");
            } else {
                TODO(sp, "sym/const");
            }
        }
        auto named_item = AST::Named<AST::Item>(sp, {}, AST::Visibility::make_bare_private(), "", AST::Item(std::move(global_asm)));
        return box$(TTStreamO(sp, ParseState(), TokenTree(Token(Token::TagTakeIP(), InterpolatedFragment(std::move(named_item))))));
    }
};

class CNakedAsmExpander: public ExpandProcMacro {
public:
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        auto o = CAsmExpander().expand(sp, crate, tt, mod);

        auto node = o->getToken().take_frag_node();
        auto* node_ap = dynamic_cast<AST::ExprNode_Asm2*>(node.get());
        ASSERT_BUG(sp, node_ap, "");
        node_ap->m_options.naked = true;

        return box$(TTStreamO(sp, ParseState(), TokenTree(Token(InterpolatedFragment(InterpolatedFragment::EXPR, node.release())))));
    }
};

STATIC_MACRO("llvm_asm", CLlvmAsmExpander);
STATIC_MACRO("asm", CAsmExpander);
STATIC_MACRO("global_asm", CGlobalAsmExpander);
STATIC_MACRO("naked_asm", CNakedAsmExpander);

#include "synext_macro.h"
#include "synext.h" // for Expand_BareExpr
#include "parse_interpolated_fragment.h"
#include "ast_crate.h"
#include "parse_ttstream.h"
#include "parse_common.h"
#include "parse_parseerror.h"

class CExpander_assert: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;

        auto lex = TTStream(sp, ParseState(), tt);
        lex.parse_state().module = &mod;

        // assertion condition
        auto n = Parse_Expr0(lex);
        ASSERT_BUG(sp, n, "No expression returned");

        ::std::vector<TokenTree> toks;

        toks.push_back(Token(TOK_RWORD_IF));
        toks.push_back(Token(TOK_EXCLAM));

        GET_TOK(tok, lex);
        if (tok == TOK_COMMA && lex.lookahead(0) == TOK_EOF) {
            GET_TOK(tok, lex);
        }
        if (tok == TOK_COMMA) {
            toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())));
            toks.push_back(Token(TOK_BRACE_OPEN));
            // User-provided message
            toks.push_back(Token(TOK_IDENT, RcString::new_interned("panic")));
            toks.push_back(Token(TOK_EXCLAM));
            toks.push_back(Token(TOK_PAREN_OPEN));

            auto fmt = Parse_Expr0(lex);
            // If there's a comma, it's a formatting sequence
            if (lex.getTokenIf(TOK_COMMA)) {
                toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, fmt.release())));

                while (lex.lookahead(0) != TOK_EOF) {
                    toks.push_back(TOK_COMMA);

                    if ((lex.lookahead(0) == TOK_IDENT || Token::type_is_rword(lex.lookahead(0))) && lex.lookahead(1) == TOK_EQUAL) {
                        toks.push_back(lex.getToken());
                        toks.push_back(lex.getToken());
                        toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, Parse_Expr0(lex).release())));
                    } else {
                        toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, Parse_Expr0(lex).release())));
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
            ::std::stringstream ss;
            n->print(ss);

            toks.push_back(Token(InterpolatedFragment(InterpolatedFragment::EXPR, n.release())));

            toks.push_back(Token(TOK_BRACE_OPEN));
            // Auto-generated message
            toks.push_back(Token(TOK_IDENT, RcString::new_interned("panic")));
            toks.push_back(Token(TOK_EXCLAM));
            toks.push_back(Token(TOK_PAREN_OPEN));
            toks.push_back(Token(TOK_STRING, std::string("assertion failed: {}"), {}));
            toks.push_back(Token(TOK_COMMA));
            toks.push_back(Token(TOK_STRING, ss.str(), {}));
            toks.push_back(Token(TOK_PAREN_CLOSE));
        } else {
            throw ParseError::Unexpected(lex, tok, {TOK_COMMA, TOK_EOF});
        }

        toks.push_back(Token(TOK_BRACE_CLOSE));

        return box$(TTStreamO(sp, ParseState(), TokenTree(AST::Edition::Rust2015, Ident::Hygiene::new_scope(), mv$(toks))));
    }
};

void Expand_init_assert() {
    Register_Synext_Macro("assert", ::std::unique_ptr<ExpandProcMacro>(new CExpander_assert));
}

#include "synext.h"
#include "parse_common.h"
#include "parse_parseerror.h"
#include "parse_tokentree.h"
#include "parse_ttstream.h"
#include "parse_lex.h" // For Codepoint
#include "ast_expr.h"
#include "ast_crate.h"

class CExpander_CompileError: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        ERROR(sp, E0000, "compile_error! " << tt);
    }
};

STATIC_MACRO("compile_error", CExpander_CompileError);

#include "synext.h"
#include "parse_common.h"
#include "parse_parseerror.h"
#include "parse_tokentree.h"
#include "parse_ttstream.h"
#include "parse_lex.h" // For Codepoint
#include "ast_expr.h"
#include "ast_crate.h"

class CConcatExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;

        auto lex = TTStream(sp, ParseState(), tt);

        ::std::string rv;
        do {
            if (LOOK_AHEAD(lex) == TOK_EOF) {
                GET_TOK(tok, lex);
                break;
            }

            auto v = Parse_Expr0(lex);
            DEBUG("concat - v=" << *v);
            Expand_BareExpr(crate, mod, v);
            DEBUG("concat[pe] - v=" << *v);
            // TODO: Visitor instead
            if (auto* vp = dynamic_cast<AST::ExprNode_String*>(v.get())) {
                rv += vp->m_value;
            } else if (auto* vp = dynamic_cast<AST::ExprNode_Integer*>(v.get())) {
                if (vp->m_datatype == CORETYPE_CHAR) {
                    rv += Codepoint{static_cast<uint32_t>(vp->m_value.truncate_u64())};
                } else {
                    rv += FMT(vp->m_value);
                }
            } else if (auto* vp = dynamic_cast<AST::ExprNode_Float*>(v.get())) {
                rv += FMT(vp->m_value);
            } else if (auto* vp = dynamic_cast<AST::ExprNode_Bool*>(v.get())) {
                rv += (vp->m_value ? "true" : "false");
            } else {
                ERROR(sp, E0000, "Unexpected expression type in concat! argument");
            }
        } while (GET_TOK(tok, lex) == TOK_COMMA);
        if (tok.type() != TOK_EOF) {
            throw ParseError::Unexpected(lex, tok, {TOK_COMMA, TOK_EOF});
        }

        return box$(TTStreamO(sp, ParseState(), TokenTree(tt.get_edition(), Token(TOK_STRING, mv$(rv), {}))));
    }
};

class CConcatIdentsExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;
        auto lex = TTStream(sp, ParseState(), tt);

        ::std::string rv;

        do {
            if (LOOK_AHEAD(lex) == TOK_EOF) {
                GET_TOK(tok, lex);
                break;
            }

            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            rv += tok.ident().name.c_str();

        } while (GET_TOK(tok, lex) == TOK_COMMA);
        if (tok.type() != TOK_EOF) {
            throw ParseError::Unexpected(lex, tok, {TOK_COMMA, TOK_EOF});
        }

        return box$(TTStreamO(sp, ParseState(), TokenTree(tt.get_edition(), Token(TOK_IDENT, Ident(lex.get_hygiene(), RcString::new_interned(rv))))));
    }
};

STATIC_MACRO("concat", CConcatExpander);
STATIC_MACRO("concat_idents", CConcatIdentsExpander);

#include "synext_macro.h"
#include "parse_common.h"
#include "parse_ttstream.h"
#include "ast_expr.h"  // ExprNode_*
#include "ast_crate.h" // edition
#include "synext.h"    // for Expand_BareExpr

namespace {
    // Read a string out of the input stream
    ::std::string get_string(const Span& sp, const AST::Crate& crate, AST::Module& mod, const TokenTree& tt) {
        auto lex = TTStream(sp, ParseState(), tt);

        auto n = Parse_ExprVal(lex);
        ASSERT_BUG(sp, n, "No expression returned");
        if (lex.lookahead(0) != TOK_EOF) {
            ERROR(sp, E0000, "Unexpected token after string literal - " << lex.getToken());
        }
        Expand_BareExpr(crate, mod, n);

        auto* string_np = dynamic_cast<AST::ExprNode_String*>(&*n);
        if (!string_np) {
            ERROR(sp, E0000, "Expected a string literal - got " << *n);
        }
        return mv$(string_np->m_value);
    }
}

class CExpanderEnv: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        ::std::string varname = get_string(sp, crate, mod, tt);

        const char* var_val_cstr = getenv(varname.c_str());
        if (!var_val_cstr) {
            ERROR(sp, E0000, "Environment variable '" << varname << "' not defined");
        }
        return box$(TTStreamO(sp, ParseState(), TokenTree(Token(TOK_STRING, ::std::string(var_val_cstr), {}))));
    }
};

class CExpanderOptionEnv: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        ::std::string varname = get_string(sp, crate, mod, tt);
        ::std::vector<TokenTree> rv;

        const char* var_val_cstr = getenv(varname.c_str());
        if (!var_val_cstr) {
            rv.reserve(7);
            rv.push_back(Token(TOK_IDENT, RcString::new_interned("None")));
            rv.push_back(Token(TOK_DOUBLE_COLON));
            rv.push_back(Token(TOK_LT));
            rv.push_back(Token(TOK_AMP));
            rv.push_back(Token(TOK_LIFETIME, RcString::new_interned("static")));
            rv.push_back(Token(TOK_IDENT, RcString::new_interned("str")));
            rv.push_back(Token(TOK_GT));
        } else {
            rv.reserve(4);
            rv.push_back(Token(TOK_IDENT, RcString::new_interned("Some")));
            rv.push_back(Token(TOK_PAREN_OPEN));
            rv.push_back(Token(TOK_STRING, ::std::string(var_val_cstr), {}));
            rv.push_back(Token(TOK_PAREN_CLOSE));
        }
        return box$(TTStreamO(sp, ParseState(), TokenTree(AST::Edition::Rust2015, {}, mv$(rv))));
    }
};

STATIC_MACRO("env", CExpanderEnv);
STATIC_MACRO("option_env", CExpanderOptionEnv);

#include "synext.h"
#include "parse_common.h"
#include "parse_ttstream.h"
#include "ast_crate.h"

namespace {
    const SpanInner_Source* get_top_span(const Span& sp) {
        return &sp.get_top_file_span();
    }
}

class CExpanderFile: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        return box$(TTStreamO(sp, ParseState(), TokenTree(Token(TOK_STRING, ::std::string(get_top_span(sp)->filename.c_str()), {}))));
    }
};

class CExpanderLine: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        return box$(TTStreamO(sp, ParseState(), TokenTree(Token(U128(get_top_span(sp)->start_line), CORETYPE_U32))));
    }
};

class CExpanderColumn: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        return box$(TTStreamO(sp, ParseState(), TokenTree(Token(U128(get_top_span(sp)->start_ofs), CORETYPE_U32))));
    }
};

class CExpanderUnstableColumn: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        return box$(TTStreamO(sp, ParseState(), TokenTree(Token(U128(get_top_span(sp)->start_ofs), CORETYPE_U32))));
    }
};

class CExpanderModulePath: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        ::std::string path_str;
        path_str += crate.m_crate_name_set;
        for (const auto& comp : mod.path().nodes) {
            path_str += "::";
            path_str += comp.c_str();
        }
        return box$(TTStreamO(sp, ParseState(), TokenTree(Token(TOK_STRING, mv$(path_str), {}))));
    }
};

STATIC_MACRO("file", CExpanderFile);
STATIC_MACRO("line", CExpanderLine);
STATIC_MACRO("column", CExpanderColumn);
STATIC_MACRO("__rust_unstable_column", CExpanderUnstableColumn);
STATIC_MACRO("module_path", CExpanderModulePath);

#include "synext_macro.h"
#include "synext.h" // for Expand_BareExpr
#include "parse_common.h"
#include "parse_parseerror.h"
#include "parse_tokentree.h"
#include "parse_ttstream.h"
#include "parse_interpolated_fragment.h"
#include "ast_crate.h" // for m_load_std
#include "ast_expr.h"  // for ExprNode_*
#include <cctype>

namespace {

    /// Options for a formatting fragment
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
        uint32_t align_char = ' ';

        Sign sign = Sign::Unspec;
        bool alternate = false;
        bool zero_pad = false;

        Debug debug_ty = Debug::Normal;

        bool width_is_arg = false;
        unsigned int width = 0;

        bool prec_is_arg = false;
        unsigned int prec = 0;

        bool operator==(const FmtArgs& x) const {
            return ::std::memcmp(this, &x, sizeof(*this)) == 0;
        }

        bool operator!=(const FmtArgs& x) const {
#define CMP(f)    \
    if (f != x.f) \
    return true
            CMP(align);
            CMP(align_char);
            CMP(sign);
            CMP(alternate);
            CMP(zero_pad);
            CMP(width_is_arg);
            CMP(width);
            CMP(prec_is_arg);
            CMP(prec);
            return false;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const FmtArgs& x) {
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
            os << "'" << x.align_char << "'";
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
            if (x.zero_pad) {
                os << "0";
            }
            os << ")";
            os << "Width(" << (x.width_is_arg ? "$" : "") << x.width << ")";
            os << "Prec(" << (x.prec_is_arg ? "$" : "") << x.prec << ")";
            return os;
        }
    };

    /// A single formatting fragment
    struct FmtFrag {
        /// Literal text preceding the fragment
        ::std::string leading_text;

        /// Argument index used
        unsigned int arg_index;

        /// Trait to use for formatting
        const char* trait_name;

        // TODO: Support case where this hasn't been edited (telling the formatter that it has nothing to apply)
        /// Options
        FmtArgs args;
    };

    uint32_t parse_utf8(const char* s, int& out_len) {
        uint8_t v1 = s[0];
        if (v1 < 0x80) {
            out_len = 1;
            return v1;
        } else if ((v1 & 0xC0) == 0x80) {
            // Invalid (continuation)
            out_len = 1;
            return 0xFFFE;
        } else if ((v1 & 0xE0) == 0xC0) {
            // Two bytes
            out_len = 2;

            uint8_t e1 = s[1];
            if ((e1 & 0xC0) != 0x80) {
                return 0xFFFE;
            }

            uint32_t outval = ((v1 & 0x1F) << 6) | ((e1 & 0x3F) << 0);
            return outval;
        } else if ((v1 & 0xF0) == 0xE0) {
            // Three bytes
            out_len = 3;
            uint8_t e1 = s[1];
            if ((e1 & 0xC0) != 0x80) {
                return 0xFFFE;
            }
            uint8_t e2 = s[2];
            if ((e2 & 0xC0) != 0x80) {
                return 0xFFFE;
            }

            uint32_t outval = ((v1 & 0x0F) << 12) | ((e1 & 0x3F) << 6) | ((e2 & 0x3F) << 0);
            return outval;
        } else if ((v1 & 0xF8) == 0xF0) {
            // Four bytes
            out_len = 4;
            uint8_t e1 = s[1];
            if ((e1 & 0xC0) != 0x80) {
                return 0xFFFE;
            }
            uint8_t e2 = s[2];
            if ((e2 & 0xC0) != 0x80) {
                return 0xFFFE;
            }
            uint8_t e3 = s[3];
            if ((e3 & 0xC0) != 0x80) {
                return 0xFFFE;
            }

            uint32_t outval = ((v1 & 0x07) << 18) | ((e1 & 0x3F) << 12) | ((e2 & 0x3F) << 6) | ((e3 & 0x3F) << 0);
            return outval;
        } else {
            throw ""; // Should be impossible.
        }
    }

    /// Parse a format string into a sequence of fragments.
    ///
    /// Returns a list of fragments, and the remaining free text after the last format sequence
    ::std::tuple<::std::vector<FmtFrag>, ::std::string> parse_format_string(const Span& sp, const ::std::string& format_string, ::std::map<RcString, unsigned int>& named, unsigned int n_free, std::vector<TokenTree>& named_args, const Ident::Hygiene& hygiene) {
        //unsigned int n_named = named.size();
        unsigned int next_free = 0;

        ::std::vector<FmtFrag> frags;
        ::std::string cur_literal;

        auto get_named = [&](RcString ident) -> unsigned {
            auto it = named.find(ident);
            if (it == named.end()) {
                // Add an implicit named argument
                it = named.insert(std::make_pair(ident, static_cast<unsigned>(named_args.size()))).first;
                // TODO: Create a token with span information pointing to this location in the string.
                if (ident == "self") {
                    // Technically, `self` needs hygiene, but mrustc doesn't do that
                    named_args.push_back(Token(TOK_RWORD_SELF));
                } else {
                    named_args.push_back(Token(TOK_IDENT, Ident(hygiene, ident)));
                }
            }
            return n_free + it->second;
        };

        const char* s = format_string.c_str();
        const char* const s_end = s + format_string.length();
        for (; s < s_end; s++) {
            if (*s != '{') {
                if (*s == '}') {
                    s++;
                    if (*s != '}') {
                        // TODO: Error? Warning?
                        s--; // Step backwards, just in case
                    }
                    // Doesn't need escaping
                    cur_literal += '}';
                } else {
                    cur_literal += *s;
                }
            } else {
                s++;
                // Escaped '{' as "{{"
                if (*s == '{') {
                    cur_literal += '{';
                    continue;
                }

                // Debugging: A view of the formatting fragment
                const char* s2 = s;
                while (s2 < s_end && *s2 != '}') {
                    s2++;
                }
                auto fmt_frag_str = ::std::string_view{s, s2};

                unsigned int index = ~0u;
                const char* trait_name;
                FmtArgs args;

                // Formatting parameter
                if (*s != ':' && *s != '}') {
                    // Parse either an integer or an identifer
                    if (isdigit(*s)) {
                        unsigned int arg_idx = 0;
                        do {
                            arg_idx *= 10;
                            arg_idx += *s - '0';
                            s++;
                        } while (isdigit(*s));
                        if (arg_idx >= n_free) {
                            ERROR(sp, E0000, "Positional argument " << arg_idx << " out of range in \"" << format_string << "\"");
                        }
                        index = arg_idx;
                    } else {
                        const char* start = s;
                        while (isalnum(*s) || *s == '_' || (*s < 0 || *s > 127)) {
                            s++;
                        }
                        index = get_named(RcString::new_interned(start, s - start));
                    }
                } else {
                    // Leave (for now)
                    // - If index is ~0u at the end of this block, it's set to the next arg
                    // - This allows {:.*} to format correctly (taking <prec> then <arg>)
                }

                // If next character is ':', parse extra information
                if (*s == ':') {
                    s++; // eat ':'

                    // Alignment
                    // - Padding character, a single unicode codepoint followed by '<'/'^'/'>'
                    {
                        int next_c_i;
                        uint32_t ch = parse_utf8(s, next_c_i);
                        char next_c = s[next_c_i];
                        if (s + next_c_i <= s_end && ch != '}' && (next_c == '<' || next_c == '^' || next_c == '>')) {
                            args.align_char = ch;
                            s += next_c_i;
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
                        //args.align = FmtArgs::Align::Unspec;
                    }

                    // Sign
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
                        //args.alternate = false;
                    }

                    if (*s == '0' && s[1] != '$') { // Special case `0$` to be an argument index, instead of zero pad
                        args.zero_pad = true;
                        s++;
                    } else {
                        //args.zero_pad = false;
                    }

                    // Padded width
                    if (::std::isdigit(*s) /*|| *s == '*'*/) {
                        unsigned int val = 0;
                        while (::std::isdigit(*s)) {
                            val *= 10;
                            val += *s - '0';
                            s++;
                        }
                        args.width = val;

                        if (*s == '$') {
                            args.width_is_arg = true;
                            s++;
                        } else {
                            //args.width_is_arg = false;
                        }
                    } else if (::std::isalpha(*s)) {
                        // Parse an ident and if the next character is $, convert to named
                        // - Otherwise keep the ident around for the formatter

                        const char* start = s;
                        while (isalnum(*s) || *s == '_' || (*s < 0 || *s > 127)) {
                            s++;
                        }
                        if (*s == '$') {
                            args.width = get_named(RcString::new_interned(start, s - start));
                            args.width_is_arg = true;

                            s++;
                        } else {
                            s = start;
                        }
                    } else {
                    }
                    // Precision
                    if (*s == '.') {
                        s++;
                        // '*' - Use next argument
                        if (*s == '*') {
                            args.prec_is_arg = true;
                            if (next_free == n_free) {
                                ERROR(sp, E0000, "Not enough arguments passed, expected at least " << n_free + 1);
                            }
                            args.prec = next_free;
                            next_free++;
                        } else if (::std::isdigit(*s)) {
                            unsigned int val = 0;
                            while (::std::isdigit(*s)) {
                                val *= 10;
                                val += *s - '0';
                                s++;
                            }
                            args.prec = val;

                            if (*s == '$') {
                                args.prec_is_arg = true;
                                s++;
                            } else {
                                //args.prec_is_arg = false;
                            }
                        } else if (::std::isalpha(*s)) {
                            // Parse an ident and if the next character is $, convert to named
                            // - Otherwise keep the ident around for the formatter

                            const char* start = s;
                            while (s != s_end && (isalnum(*s) || *s == '_' || (*s < 0 || *s > 127))) {
                                s++;
                            }
                            if (*s == '$') {
                                args.prec = get_named(RcString::new_interned(start, s - start));
                                args.prec_is_arg = true;

                                s++;
                            } else {
                                s = start;
                                //ERROR(sp, E0000, "Unexpected character in precision");
                            }
                        } else {
                            // Wut?
                            ERROR(sp, E0000, "Unexpected character in precision");
                        }
                    }

                    if (s == s_end) {
                        ERROR(sp, E0000, "Unexpected end of formatting string");
                    }

                    // Parse ident?
                    // - Lazy way is to just handle a single char and ensure that it is just a single char
                    if (s[0] == '}') {
                        trait_name = "Display";
                    } else if (s[1] == '}') {
                        switch (s[0]) {
                            default:
                                ERROR(sp, E0000, "Unknown formatting type specifier '" << *s << "'");
                            case '?':
                                s++;
                                trait_name = "Debug";
                                break;
                            case 'b':
                                s++;
                                trait_name = "Binary";
                                break;
                            case 'o':
                                s++;
                                trait_name = "Octal";
                                break;
                            case 'x':
                                s++;
                                trait_name = "LowerHex";
                                break;
                            case 'X':
                                s++;
                                trait_name = "UpperHex";
                                break;
                            case 'p':
                                s++;
                                trait_name = "Pointer";
                                break;
                            case 'e':
                                s++;
                                trait_name = "LowerExp";
                                break;
                            case 'E':
                                s++;
                                trait_name = "UpperExp";
                                break;
                        }
                        assert(*s == '}');
                    } else {
                        if (strncmp(s, "x?}", 3) == 0) {
                            args.debug_ty = FmtArgs::Debug::LowerHex;
                            trait_name = "Debug";
                        } else if (strncmp(s, "X?}", 3) == 0) {
                            args.debug_ty = FmtArgs::Debug::UpperHex;
                            trait_name = "Debug";
                        } else {
                            TODO(sp, "Parse formatting fragment at \"" << fmt_frag_str << "\" (long type) - s=...\"" << s << "\"");
                        }
                    }
                } else {
                    if (*s != '}') {
                        ERROR(sp, E0000, "Malformed formatting fragment, unexpected " << *s);
                    }
                    // Otherwise, it's just a trivial Display call
                    trait_name = "Display";
                }

                // Set index if unspecified
                if (index == ~0u) {
                    if (next_free == n_free) {
                        ERROR(sp, E0000, "Not enough arguments passed, expected at least " << n_free + 1);
                    }
                    index = next_free;
                    next_free++;
                }

                frags.push_back(FmtFrag{mv$(cur_literal), index, trait_name, mv$(args)});
            }
        }

        return ::std::make_tuple(mv$(frags), mv$(cur_literal));
    }
}

namespace {
    Token ident(const char* s) {
        return Token(TOK_IDENT, RcString::new_interned(s));
    }

    void push_path(::std::vector<TokenTree>& toks, const AST::Crate& crate, ::std::initializer_list<const char*> il) {
        AST::AbsolutePath ap;
        // TODO: Inject a path fragment (interpolated path), to avoid edition parsing quirks
        switch (crate.m_load_std) {
            case ::AST::Crate::LOAD_NONE:
                break;
            case ::AST::Crate::LOAD_CORE:
                ASSERT_BUG(Span(), crate.m_ext_cratename_core != "", "");
                ap.crate = crate.m_ext_cratename_core;
                break;
            case ::AST::Crate::LOAD_STD:
                //ap.crate = "=std";
                ASSERT_BUG(Span(), crate.m_ext_cratename_core != "", "");
                ap.crate = crate.m_ext_cratename_core;
                break;
        }
        for (auto ent : il) {
            // TODO: This could be slow (looking up the interned string), but most of these are repeated a LOT
            ap.nodes.push_back(RcString::new_interned(ent));
        }
        toks.push_back(Token(InterpolatedFragment(std::move(ap))));
    }

    void push_toks(::std::vector<TokenTree>& toks, Token t1) {
        toks.push_back(mv$(t1));
    }

    void push_toks(::std::vector<TokenTree>& toks, Token t1, Token t2) {
        toks.push_back(mv$(t1));
        toks.push_back(mv$(t2));
    }

    //void push_toks(::std::vector<TokenTree>& toks, Token t1, Token t2, Token t3) {
    //    toks.push_back( mv$(t1) );
    //    toks.push_back( mv$(t2) );
    //    toks.push_back( mv$(t3) );
    //}
    void push_toks(::std::vector<TokenTree>& toks, Token t1, Token t2, Token t3, Token t4) {
        toks.push_back(mv$(t1));
        toks.push_back(mv$(t2));
        toks.push_back(mv$(t3));
        toks.push_back(mv$(t4));
    }

    ::std::unique_ptr<TokenStream> expand_format_args(const Span& sp, const ::AST::Crate& crate, TTStream& lex, bool add_newline) {
        Token tok;

        auto format_string_node = Parse_ExprVal(lex);
        ASSERT_BUG(sp, format_string_node, "No expression returned");
        Expand_BareExpr(crate, lex.parse_state().get_current_mod(), format_string_node);

        auto* format_string_np = dynamic_cast<AST::ExprNode_String*>(&*format_string_node);
        if (!format_string_np) {
            ERROR(sp, E0000, "format_args! requires a string literal - got " << *format_string_node);
        }
        const auto& format_string_sp = format_string_np->span();
        const auto& format_string = format_string_np->m_value;
        auto h = format_string_np->m_hygiene;

        ::std::map<RcString, unsigned int> named_args_index;
        ::std::vector<TokenTree> named_args;
        ::std::vector<TokenTree> free_args;

        // - Parse the arguments
        while (GET_TOK(tok, lex) == TOK_COMMA) {
            if (lex.lookahead(0) == TOK_EOF) {
                GET_TOK(tok, lex);
                break;
            }

            // - Named parameters
            if ((lex.lookahead(0) == TOK_IDENT || Token::type_is_rword(lex.lookahead(0))) && lex.lookahead(1) == TOK_EQUAL) {
                GET_TOK(tok, lex);
                auto name = tok.type() == TOK_IDENT ? tok.ident().name : RcString::new_interned(tok.to_str());
                DEBUG("Named `" << name << "`");

                GET_CHECK_TOK(tok, lex, TOK_EQUAL);

                auto expr_tt = TokenTree(Token(InterpolatedFragment(InterpolatedFragment::EXPR, Parse_Expr0(lex).release())));

                auto ins_rv = named_args_index.insert(::std::make_pair(mv$(name), static_cast<unsigned>(named_args.size())));
                if (ins_rv.second == false) {
                    ERROR(sp, E0000, "Duplicate definition of named argument `" << ins_rv.first->first << "`");
                }
                named_args.push_back(mv$(expr_tt));
            }
            // - Free parameters
            else {
                DEBUG("Free");
                auto expr_tt = TokenTree(Token(InterpolatedFragment(InterpolatedFragment::EXPR, Parse_Expr0(lex).release())));
                free_args.push_back(mv$(expr_tt));
            }
        }
        CHECK_TOK(tok, TOK_EOF);

        // - Parse the format string
        ::std::vector<FmtFrag> fragments;
        ::std::string tail;
        ::std::tie(fragments, tail) = parse_format_string(format_string_sp, format_string, named_args_index, free_args.size(), named_args, h);
        if (add_newline) {
            tail += "\n";
        }

        bool is_simple = true;
        for (unsigned int i = 0; i < fragments.size(); i++) {
            if (fragments[i].arg_index != i) {
                DEBUG(i << "Ordering mismach");
                is_simple = false;
            }
            if (fragments[i].args != FmtArgs{}) {
                DEBUG(i << " Args changed - " << fragments[i].args << " != " << FmtArgs{});
                is_simple = false;
            }
        }

        ::std::vector<TokenTree> toks;
        // This should expand to a `match (a, b, c) { (ref _0, ref _1, ref _2) => ... }` to ensure that the values live long enough?
        // - Also avoids name collisions
        toks.push_back(TokenTree(TOK_RWORD_MATCH));
        toks.push_back(TokenTree(TOK_PAREN_OPEN));
        for (auto& arg : free_args) {
            toks.push_back(TokenTree(TOK_AMP));
            toks.push_back(mv$(arg));
            toks.push_back(TokenTree(TOK_COMMA));
        }
        for (auto& arg : named_args) {
            toks.push_back(TokenTree(TOK_AMP));
            toks.push_back(mv$(arg));
            toks.push_back(TokenTree(TOK_COMMA));
        }
        toks.push_back(TokenTree(TOK_PAREN_CLOSE));
        toks.push_back(TokenTree(TOK_BRACE_OPEN));
        toks.push_back(TokenTree(TOK_PAREN_OPEN));
        for (unsigned int i = 0; i < free_args.size() + named_args.size(); i++) {
            toks.push_back(ident(FMT("a" << i).c_str()));
            toks.push_back(TokenTree(TOK_COMMA));
        }
        toks.push_back(TokenTree(TOK_PAREN_CLOSE));
        toks.push_back(TokenTree(TOK_FATARROW));
        toks.push_back(TokenTree(TOK_BRACE_OPEN));

        // Save fragments into a static
        // `static FRAGMENTS: [&'static str; N] = [...];`
        // - Contains N+1 entries, where N is the number of fragments
        {
            toks.push_back(TokenTree(TOK_RWORD_STATIC));
            toks.push_back(ident("FRAGMENTS"));
            toks.push_back(TokenTree(TOK_COLON));

            toks.push_back(TokenTree(TOK_SQUARE_OPEN));
            toks.push_back(Token(TOK_AMP));
            toks.push_back(Token(TOK_LIFETIME, RcString::new_interned("static")));
            toks.push_back(ident("str"));
            toks.push_back(Token(TOK_SEMICOLON));
            toks.push_back(Token(U128(fragments.size() + 1), CORETYPE_UINT));
            toks.push_back(TokenTree(TOK_SQUARE_CLOSE));

            toks.push_back(Token(TOK_EQUAL));

            toks.push_back(TokenTree(TOK_SQUARE_OPEN));
            for (const auto& frag : fragments) {
                toks.push_back(Token(TOK_STRING, frag.leading_text, h));
                toks.push_back(TokenTree(TOK_COMMA));
            }
            toks.push_back(Token(TOK_STRING, tail, h));
            toks.push_back(TokenTree(TOK_SQUARE_CLOSE));

            toks.push_back(Token(TOK_SEMICOLON));
        }

        struct H {
            static void argument_list(::std::vector<TokenTree>& toks, const ::std::vector<FmtFrag>& fragments, const AST::Crate& crate) {
                toks.push_back(TokenTree(TOK_AMP));
                toks.push_back(TokenTree(TOK_SQUARE_OPEN));
                for (const auto& frag : fragments) {
                    // In 1.90.0, there's a collection of functions like `new_display`, one for each trait
                    // Hacky option: Convert `LowerHex` into `_lower_hex`
                    ::std::stringstream new_fn_ss;
                    new_fn_ss << "new";
                    for (const char* s = frag.trait_name; *s; s++) {
                        if (isupper(*s)) {
                            new_fn_ss << "_" << char(tolower(*s));
                        } else {
                            new_fn_ss << *s;
                        }
                    }
                    push_path(toks, crate, {"fmt", "rt", "Argument", new_fn_ss.str().c_str()});
                    toks.push_back(Token(TOK_PAREN_OPEN));
                    toks.push_back(ident(FMT("a" << frag.arg_index).c_str()));
                    toks.push_back(Token(TOK_PAREN_CLOSE));
                    toks.push_back(TokenTree(TOK_COMMA));
                }
                toks.push_back(TokenTree(TOK_SQUARE_CLOSE));
            }
        };

        if (is_simple) {
            // ::fmt::Arguments::new_v1
            push_path(toks, crate, {"fmt", "Arguments", "new_v1"});
            // (
            toks.push_back(TokenTree(TOK_PAREN_OPEN));
            {
                toks.push_back(TokenTree(TOK_AMP));
                toks.push_back(ident("FRAGMENTS"));
                toks.push_back(TokenTree(TOK_COMMA));

                H::argument_list(toks, fragments, crate);
            }
            // )
            toks.push_back(TokenTree(TOK_PAREN_CLOSE));
        } else // if(is_simple)
        {
            // 1. Generate a set of arguments+formatters
            // > Each combination of argument index and fragment type needs a unique entry in the `args` array

            // Use new_v1_formatted
            // - requires creating more entries in the `args` list to cover multiple formatters for one value
            push_path(toks, crate, {"fmt", "Arguments", "new_v1_formatted"});
            // (
            toks.push_back(TokenTree(TOK_PAREN_OPEN));
            {
                toks.push_back(TokenTree(TOK_AMP));
                toks.push_back(ident("FRAGMENTS"));
                toks.push_back(TokenTree(TOK_COMMA));

                // TODO: Fragments to format
                // - The format stored by mrustc doesn't quite work with how rustc (and fmt::rt::v1) works
                H::argument_list(toks, fragments, crate);
                toks.push_back(TokenTree(TOK_COMMA));

                toks.push_back(TokenTree(TOK_AMP));
                toks.push_back(TokenTree(TOK_SQUARE_OPEN));
                for (const auto& frag : fragments) {
                    push_path(toks, crate, {"fmt", "rt", "Placeholder"});
                    toks.push_back(TokenTree(TOK_BRACE_OPEN));

                    push_toks(toks, ident("position"), TOK_COLON);
                    push_toks(toks, Token(U128(&frag - fragments.data()), CORETYPE_UINT));
                    push_toks(toks, TOK_COMMA);

                    // Flags
                    {
                        push_toks(toks, ident("flags"), TOK_COLON);

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

                        uint64_t flags = 0;
                        // ::core::fmt::FlagV1 (private)
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
                        if (frag.args.zero_pad) {
                            flags |= 1 << Flag::SignAwareZeroPad;
                        }
                        switch (frag.args.debug_ty) {
                            case FmtArgs::Debug::Normal:
                                break;
                            case FmtArgs::Debug::LowerHex:
                                flags |= 1 << Flag::DebugLowerHex;
                                break;
                            case FmtArgs::Debug::UpperHex:
                                flags |= 1 << Flag::DebugUpperHex;
                                break;
                        }
                        // Flags shifted, with 21 being SignPlus now
                        // See `rustc-1.90.0-src/library/core/src/fmt/mod.rs` `mod flags`
                        flags <<= 21;
                        // NOTE: The fill character is in the low 21 bits (max size of a codepoint)
                        flags |= frag.args.align_char & 0x1FFFFF;

                        // Width and precision flags
                        if (frag.args.width_is_arg || frag.args.width != 0) {
                            flags |= 1 << 27;
                        }
                        if (frag.args.prec_is_arg || frag.args.prec != 0) {
                            flags |= 1 << 28;
                        }

                        // Alignment is encoded as a flag.
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
                        push_toks(toks, Token(U128(flags), CORETYPE_U32));
                        push_toks(toks, TOK_COMMA);
                    }
                    // Counts (precision and width)
                    {
                        auto push_path_count = [&](const char* variant) {
                            push_path(toks, crate, {"fmt", "rt", "Count", variant});
                        };

                        push_toks(toks, ident("precision"), TOK_COLON);
                        if (frag.args.prec_is_arg || frag.args.prec != 0) {
                            push_path_count("Is");
                            push_toks(toks, TOK_PAREN_OPEN);
                            if (frag.args.prec_is_arg) {
                                push_toks(toks, TOK_STAR, ident(FMT("a" << frag.args.prec).c_str()));
                                push_toks(toks, TOK_RWORD_AS, ident("u16"));
                            } else {
                                push_toks(toks, Token(U128(frag.args.prec), CORETYPE_U16));
                            }
                            toks.push_back(TokenTree(TOK_PAREN_CLOSE));
                        } else {
                            push_path_count("Implied");
                        }
                        toks.push_back(TokenTree(TOK_COMMA));

                        push_toks(toks, ident("width"), TOK_COLON);
                        if (frag.args.width_is_arg || frag.args.width != 0) {
                            push_path_count("Is");
                            push_toks(toks, TOK_PAREN_OPEN);
                            if (frag.args.width_is_arg) {
                                push_toks(toks, TOK_STAR, ident(FMT("a" << frag.args.width).c_str()));
                                push_toks(toks, TOK_RWORD_AS, ident("u16"));
                            } else {
                                push_toks(toks, Token(U128(frag.args.width), CORETYPE_U16));
                            }
                            toks.push_back(TokenTree(TOK_PAREN_CLOSE));
                        } else {
                            push_path_count("Implied");
                        }
                        toks.push_back(TokenTree(TOK_COMMA));
                    }

                    toks.push_back(TokenTree(TOK_BRACE_CLOSE));
                    toks.push_back(TokenTree(TOK_COMMA));
                }
                toks.push_back(TokenTree(TOK_SQUARE_CLOSE));
            }
            // )
            toks.push_back(TokenTree(TOK_PAREN_CLOSE));
        } // if(is_simple) else

        toks.push_back(TokenTree(TOK_BRACE_CLOSE));
        toks.push_back(TokenTree(TOK_BRACE_CLOSE));

        return box$(TTStreamO(sp, ParseState(), TokenTree(lex.get_edition(), Ident::Hygiene::new_scope(), mv$(toks))));
    }
}

class CFormatArgsExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;

        auto lex = TTStream(sp, ParseState(), tt);
        lex.parse_state().module = &mod;

        return expand_format_args(sp, crate, lex, /*add_newline=*/false);
    }
};

class CConstFormatArgsExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;

        auto lex = TTStream(sp, ParseState(), tt);
        lex.parse_state().module = &mod;

        return expand_format_args(sp, crate, lex, /*add_newline=*/false);
    }
};

class CFormatArgsNlExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;

        auto lex = TTStream(sp, ParseState(), tt);
        lex.parse_state().module = &mod;

        return expand_format_args(sp, crate, lex, /*add_newline=*/true);
    }
};

STATIC_MACRO("format_args", CFormatArgsExpander);
STATIC_MACRO("const_format_args", CConstFormatArgsExpander);
STATIC_MACRO("format_args_nl", CFormatArgsNlExpander);

#undef CMP

#include "synext_macro.h"
#include "synext.h" // for Expand_BareExpr
#include "parse_common.h"
#include "parse_parseerror.h" // for GET_CHECK_TOK
#include "parse_ttstream.h"
#include "parse_lex.h" // Lexer (new files)
#include "ast_expr.h"
#include "ast_crate.h"

namespace {

    ::std::string include_get_string(const Span& sp, TokenStream& lex, const ::AST::Crate& crate, AST::Module& mod) {
        auto n = Parse_ExprVal(lex);
        ASSERT_BUG(sp, n, "No expression returned");
        Expand_BareExpr(crate, mod, n);

        auto* string_np = dynamic_cast<AST::ExprNode_String*>(&*n);
        if (!string_np) {
            ERROR(sp, E0000, "include! requires a string literal - got " << *n);
        }
        return mv$(string_np->m_value);
    }

    ::std::string get_path_relative_to(const ::std::string& base_path, ::std::string path) {
        DEBUG(base_path << ", " << path);
        // Absolute
        if (path[0] == '/') {
            return path;
        }
        if (base_path.size() == 0) {
            return path;
        } else if (base_path.back() == '/') {
            return base_path + path;
        } else {
            auto slash = base_path.find_last_of('/');
            if (slash == ::std::string::npos) {
                return path;
            } else {
                DEBUG("> slash = " << slash);
                slash += 1;
                ::std::string rv;
                rv.reserve(slash + path.size());
                rv.append(base_path.begin(), base_path.begin() + slash);
                rv.append(path.begin(), path.end());
                return rv;
            }
        }
    }
};

class CIncludeExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;
        auto lex = TTStream(sp, ParseState(), tt);

        auto path = include_get_string(sp, lex, crate, mod);
        GET_CHECK_TOK(tok, lex, TOK_EOF);

        //::std::string file_path = get_path_relative_to(mod.m_file_info.path, mv$(path));
        ::std::string file_path = get_path_relative_to(sp.get_top_file_span().filename.c_str(), mv$(path));
        crate.m_extra_files.push_back(file_path);

        try {
            ParseState ps;
            ps.module = &mod;
            DEBUG("Edition = " << crate.m_edition);
            return box$(Lexer(file_path, crate.m_edition, ps));
        } catch (::std::runtime_error& e) {
            ERROR(sp, E0000, e.what());
        }
    }
};

class CIncludeBytesExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;
        auto lex = TTStream(sp, ParseState(), tt);

        auto path = include_get_string(sp, lex, crate, mod);
        GET_CHECK_TOK(tok, lex, TOK_EOF);

        ::std::string file_path = get_path_relative_to(mod.m_file_info.path, mv$(path));
        crate.m_extra_files.push_back(file_path);

        ::std::ifstream is(file_path);
        if (!is.good()) {
            ERROR(sp, E0000, "Cannot open file " << file_path << " for include_bytes!");
        }
        ::std::stringstream ss;
        ss << is.rdbuf();

        ::std::vector<TokenTree> toks;
        toks.push_back(Token(TOK_BYTESTRING, mv$(ss.str()), {}));
        return box$(TTStreamO(sp, ParseState(), TokenTree(AST::Edition::Rust2015, Ident::Hygiene::new_scope(), mv$(toks))));
    }
};

class CIncludeStrExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;
        auto lex = TTStream(sp, ParseState(), tt);

        auto path = include_get_string(sp, lex, crate, mod);
        GET_CHECK_TOK(tok, lex, TOK_EOF);

        ::std::string file_path = get_path_relative_to(mod.m_file_info.path, mv$(path));
        crate.m_extra_files.push_back(file_path);

        ::std::ifstream is(file_path);
        if (!is.good()) {
            ERROR(sp, E0000, "Cannot open file " << file_path << " for include_str!");
        }
        ::std::stringstream ss;
        ss << is.rdbuf();

        ::std::vector<TokenTree> toks;
        toks.push_back(Token(TOK_STRING, mv$(ss.str()), {}));
        return box$(TTStreamO(sp, ParseState(), TokenTree(AST::Edition::Rust2015, Ident::Hygiene::new_scope(), mv$(toks))));
    }
};

// TODO: include_str! and include_bytes!

STATIC_MACRO("include", CIncludeExpander);
STATIC_MACRO("include_bytes", CIncludeBytesExpander);
STATIC_MACRO("include_str", CIncludeStrExpander);

#include "synext_macro.h"
#include "parse_interpolated_fragment.h"
#include "ast_crate.h"
#include "hir_hir.h"
#include "parse_ttstream.h"
#include "parse_common.h"
#include "parse_parseerror.h"

class CExpander_panic: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;

        auto edition = crate.m_edition;
        if (tt.hygiene().has_mod_path() && tt.hygiene().mod_path().crate != "") {
            edition = crate.m_extern_crates.at(tt.hygiene().mod_path().crate).m_hir->m_edition;
        }
        ::std::vector<TokenTree> toks;
        toks.push_back(Token(TOK_DOUBLE_COLON));
        toks.push_back(Token(TOK_STRING, std::string(crate.m_ext_cratename_core.c_str()), {}));
        toks.push_back(Token(TOK_DOUBLE_COLON));
        toks.push_back(Token(TOK_IDENT, RcString::new_interned("panic")));
        toks.push_back(Token(TOK_DOUBLE_COLON));
        switch (crate.m_edition) {
            case AST::Edition::Rust2015:
            case AST::Edition::Rust2018:
                toks.push_back(Token(TOK_IDENT, RcString::new_interned("panic_2015")));
                break;
            case AST::Edition::Rust2021:
            case AST::Edition::Rust2024:
                toks.push_back(Token(TOK_IDENT, RcString::new_interned("panic_2021")));
                break;
        }
        toks.push_back(Token(TOK_EXCLAM));
        toks.push_back(Token(TOK_PAREN_OPEN));
        if (tt.size() > 0) {
            toks.push_back(tt.clone());
        }
        toks.push_back(Token(TOK_PAREN_CLOSE));

        return box$(TTStreamO(sp, ParseState(), TokenTree(edition, Ident::Hygiene::new_scope(), mv$(toks))));
    }
};

class CExpander_unreachable: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const ::AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;

        auto edition = crate.m_edition;
        if (tt.hygiene().has_mod_path() && tt.hygiene().mod_path().crate != "") {
            edition = crate.m_extern_crates.at(tt.hygiene().mod_path().crate).m_hir->m_edition;
        }
        ::std::vector<TokenTree> toks;
        toks.push_back(Token(TOK_DOUBLE_COLON));
        toks.push_back(Token(TOK_STRING, std::string(crate.m_ext_cratename_core.c_str()), {}));
        toks.push_back(Token(TOK_DOUBLE_COLON));
        toks.push_back(Token(TOK_IDENT, RcString::new_interned("panic")));
        toks.push_back(Token(TOK_DOUBLE_COLON));
        switch (crate.m_edition) {
            case AST::Edition::Rust2015:
            case AST::Edition::Rust2018:
                toks.push_back(Token(TOK_IDENT, RcString::new_interned("unreachable_2015")));
                break;
            case AST::Edition::Rust2021:
            case AST::Edition::Rust2024:
                toks.push_back(Token(TOK_IDENT, RcString::new_interned("unreachable_2021")));
                break;
        }
        toks.push_back(Token(TOK_EXCLAM));
        toks.push_back(Token(TOK_PAREN_OPEN));
        if (tt.size() > 0) {
            toks.push_back(tt.clone());
        }
        toks.push_back(Token(TOK_PAREN_CLOSE));

        return box$(TTStreamO(sp, ParseState(), TokenTree(edition, Ident::Hygiene::new_scope(), mv$(toks))));
    }
};

void Expand_init_panic() {
    Register_Synext_Macro("panic", ::std::unique_ptr<ExpandProcMacro>(new CExpander_panic));
    Register_Synext_Macro("unreachable", ::std::unique_ptr<ExpandProcMacro>(new CExpander_unreachable));
}

#include "synext.h"
#include "parse_parseerror.h" // For GET_CHECK_TOK
#include "parse_common.h"     // TokenTree etc
#include "parse_ttstream.h"
#include "ast_crate.h"

class CExpanderRegisterDiagnostic: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        return box$(TTStreamO(sp, ParseState(), TokenTree()));
    }
};

class CExpanderDiagnosticUsed: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        return box$(TTStreamO(sp, ParseState(), TokenTree()));
    }
};

class CExpanderBuildDiagnosticArray: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        auto lex = TTStream(sp, ParseState(), tt);

        Token tok;

        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        //auto crate_name = mv$(tok.str());
        GET_CHECK_TOK(tok, lex, TOK_COMMA);
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto item_name = tok.ident();
        GET_CHECK_TOK(tok, lex, TOK_EOF);

        ::std::vector<TokenTree> toks;
        toks.push_back(TOK_RWORD_PUB);
        toks.push_back(TOK_RWORD_STATIC);
        toks.push_back(Token(TOK_IDENT, item_name));
        // : [(&'static str, &'static str); 0]
        toks.push_back(TOK_COLON);
        toks.push_back(TOK_SQUARE_OPEN);
        toks.push_back(TOK_PAREN_OPEN);
        toks.push_back(TOK_AMP);
        toks.push_back(Token(TOK_LIFETIME, RcString::new_interned("static")));
        toks.push_back(Token(TOK_IDENT, RcString::new_interned("str")));
        toks.push_back(TOK_COMMA);
        toks.push_back(TOK_AMP);
        toks.push_back(Token(TOK_LIFETIME, RcString::new_interned("static")));
        toks.push_back(Token(TOK_IDENT, RcString::new_interned("str")));
        toks.push_back(TOK_PAREN_CLOSE);
        toks.push_back(TOK_SEMICOLON);
        toks.push_back(Token(U128(0), CORETYPE_UINT));
        toks.push_back(TOK_SQUARE_CLOSE);
        // = [];
        toks.push_back(TOK_EQUAL);
        toks.push_back(TOK_SQUARE_OPEN);
        toks.push_back(TOK_SQUARE_CLOSE);
        toks.push_back(TOK_SEMICOLON);

        return box$(TTStreamO(sp, ParseState(), TokenTree(AST::Edition::Rust2015, lex.get_hygiene(), mv$(toks))));
    }
};

STATIC_MACRO("__register_diagnostic", CExpanderRegisterDiagnostic)
STATIC_MACRO("__diagnostic_used", CExpanderDiagnosticUsed)
STATIC_MACRO("__build_diagnostic_array", CExpanderBuildDiagnosticArray)

#include "synext.h"
#include "ast_crate.h"
#include "parse_common.h"
#include "parse_ttstream.h"

class CExpander: public ExpandProcMacro {
    ::std::unique_ptr<TokenStream> expand(const Span& sp, const AST::Crate& crate, const TokenTree& tt, AST::Module& mod) override {
        Token tok;
        ::std::string rv;

        auto lex = TTStream(sp, ParseState(), tt);
        while (GET_TOK(tok, lex) != TOK_EOF) {
            if (!rv.empty()) {
                rv += " ";
            }
            DEBUG(" += " << tok);
            if (tok.type() == TOK_IDENT) {
                rv += tok.ident().name.c_str();
            } else {
                auto v = tok.to_str();
                const char* s = v.c_str();
                // Very hacky strip of hygine information (e.g. from paths)
                if (s[0] == '{' && s[1]) {
                    while (*s != '}' && *s) {
                        s++;
                    }
                    assert(*s);
                    s++;
                }
                rv += s;
            }
        }

        // TODO: Strip out any `{...}` sequences that aren't from nested
        // strings.

        return box$(TTStreamO(sp, ParseState(), TokenTree(Token(TOK_STRING, mv$(rv), {}))));
    }
};

STATIC_MACRO("stringify", CExpander);
