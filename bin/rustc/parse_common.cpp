#include "parse_common.h"

#include "parse_parseerror.h"
#include "ast_ast.h"
#include "ast_expr.h"
#include "parse_common.h"
#include <iostream>
#include "parse_tokentree.h"
#include "parse_interpolated_fragment.h"

using AST::ExprNode;
using AST::ExprNodeP;

// TODO: Use a ProtoSpan instead of a point span?
static inline ExprNodeP mk_exprnodep(const TokenStream& lex, AST::ExprNode* en) {
    en->set_span(lex.point_span());
    return ExprNodeP(en);
}

#define NEWNODE(type, ...) mk_exprnodep(lex, new type(__VA_ARGS__))

ExprNodeP Parse_ExprBlockNode(TokenStream& lex, AST::ExprNode_Block::Type ty, Ident label = Ident(""));
//ExprNodeP Parse_ExprBlockLine_WithItems(TokenStream& lex, ::std::shared_ptr<AST::Module>& local_mod, bool& add_silence_if_end);
//ExprNodeP Parse_ExprBlockLine(TokenStream& lex, bool *add_silence);
ExprNodeP Parse_ExprBlockLine_Stmt(TokenStream& lex, bool& has_semicolon);
//ExprNodeP Parse_Stmt(TokenStream& lex);   // common.h
ExprNodeP Parse_Stmt_Let(TokenStream& lex);
ExprNodeP Parse_Expr0(TokenStream& lex);
ExprNodeP Parse_Expr1_5(TokenStream& lex); // Boolean OR
ExprNodeP Parse_Expr3(TokenStream& lex);
ExprNodeP Parse_IfStmt(TokenStream& lex);
ExprNodeP Parse_WhileStmt(TokenStream& lex, Ident lifetime);
ExprNodeP Parse_ForStmt(TokenStream& lex, Ident lifetime);
ExprNodeP Parse_Expr_Match(TokenStream& lex);
ExprNodeP Parse_Expr1(TokenStream& lex);
ExprNodeP Parse_ExprFC(TokenStream& lex);
ExprNodeP Parse_ExprMacro(TokenStream& lex, AST::Path tok);

AST::Expr Parse_Expr(TokenStream& lex) {
    return ::AST::Expr(Parse_Expr0(lex));
}

AST::Expr Parse_ExprBlock(TokenStream& lex) {
    return ::AST::Expr(Parse_ExprBlockNode(lex));
}

AST::ExprNodeP Parse_ExprBlockNode(TokenStream& lex) {
    return Parse_ExprBlockNode(lex, AST::ExprNode_Block::Type::Bare, RcString());
}

ExprNodeP Parse_ExprBlockNode(TokenStream& lex, AST::ExprNode_Block::Type ty /*=Bare*/, Ident label /*=RcString()*/) {
    TRACE_FUNCTION;
    CLEAR_PARSE_FLAGS_EXPR(lex);
    Token tok;

    ::std::vector<AST::ExprNode_Block::Line> lines;
    AST::AttributeList attrs;

    auto orig_module = lex.parse_state().module;
    ::std::shared_ptr<AST::Module> local_mod;

    if (LOOK_AHEAD(lex) == TOK_INTERPOLATED_BLOCK) {
        GET_TOK(tok, lex);
        return tok.take_frag_node();
    }

    GET_CHECK_TOK(tok, lex, TOK_BRACE_OPEN);

    while (LOOK_AHEAD(lex) != TOK_BRACE_CLOSE) {
        Parse_ParentAttrs(lex, attrs);
        if (LOOK_AHEAD(lex) == TOK_BRACE_CLOSE) {
            break;
        }

        bool add_silence_if_end = false;
        // `add_silence_if_end` indicates that the statement had a semicolon.
        auto rv = Parse_ExprBlockLine_WithItems(lex, local_mod, add_silence_if_end);
        if (rv) {
            // Set to TRUE if there was no semicolon after a statement
            lines.push_back({add_silence_if_end, mv$(rv)});
        } else {
            assert(!add_silence_if_end);
        }
    }
    GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);

    if (lex.parse_state().module != orig_module) {
        DEBUG("Restore module from " << lex.parse_state().module->path() << " to " << orig_module->path());
        lex.parse_state().module = orig_module;
    }
    auto* rv_blk = new ::AST::ExprNode_Block(ty, mv$(lines), mv$(local_mod));
    rv_blk->m_label = label;
    auto rv = ExprNodeP(rv_blk);
    rv->set_attrs(mv$(attrs));
    return rv;
}

/// Parse a single line in a block, handling items added to the local module
///
/// - If an item was parsed, this returns an empty ExprNodeP
ExprNodeP Parse_ExprBlockLine_WithItems(TokenStream& lex, ::std::shared_ptr<AST::Module>& local_mod, bool& add_silence_if_end) {
    Token tok;

    auto item_attrs = Parse_ItemAttrs(lex);
    GET_TOK(tok, lex);

    // An item statement remains an opaque `stmt` fragment while it is
    // forwarded through macro matchers.  Materialise the contained item only
    // when the fragment is parsed in statement position.
    if (tok.type() == TOK_INTERPOLATED_STMT_ITEM) {
        if (!local_mod) {
            local_mod = lex.parse_state().get_current_mod().add_anon();
            DEBUG("Set module from " << lex.parse_state().module->path() << " to " << local_mod->path());
            lex.parse_state().module = local_mod.get();
        }
        auto item = tok.take_frag_stmt_item();
        for (auto& attr : item_attrs.m_items) {
            item.attrs.m_items.push_back(mv$(attr));
        }
        local_mod->add_item(mv$(item));
        return ExprNodeP();
    }

    // `union Ident` - contextual keyword
    if (tok.type() == TOK_IDENT && tok.ident().name == "union" && lex.lookahead(0) == TOK_IDENT) {
        PUTBACK(tok, lex);
        if (!local_mod) {
            local_mod = lex.parse_state().get_current_mod().add_anon();
            DEBUG("Set module from " << lex.parse_state().module->path() << " to " << local_mod->path());
            lex.parse_state().module = local_mod.get();
        }
        Parse_Mod_Item(lex, *local_mod, mv$(item_attrs));
        return ExprNodeP();
    }

    if (tok.type() == TOK_IDENT && tok.ident().name == "macro_rules" && lex.lookahead(0) == TOK_EXCLAM) {
        // Special case - create a local module if macro_rules! is seen
        // - Allows correct scoping of defined macros
        if (!local_mod) {
            local_mod = lex.parse_state().get_current_mod().add_anon();
            DEBUG("Set module from " << lex.parse_state().module->path() << " to " << local_mod->path());
            lex.parse_state().module = local_mod.get();
        }
    }

    switch (tok.type()) {
        // Items:
        case TOK_RWORD_CRATE:
            if (lex.lookahead(0) == TOK_DOUBLE_COLON) {
                break;
            }
        case TOK_INTERPOLATED_VIS:
        case TOK_INTERPOLATED_ITEM:
        case TOK_RWORD_PUB:
            // NOTE: Allowed, but doesn't do much
        case TOK_RWORD_TYPE:
        case TOK_RWORD_USE:
        case TOK_RWORD_EXTERN:
        case TOK_RWORD_STATIC:
        case TOK_RWORD_STRUCT:
        case TOK_RWORD_MACRO:
        case TOK_RWORD_ENUM:
        case TOK_RWORD_TRAIT:
        case TOK_RWORD_IMPL:
        case TOK_RWORD_FN:
        case TOK_RWORD_MOD:
            PUTBACK(tok, lex);
            if (!local_mod) {
                local_mod = lex.parse_state().get_current_mod().add_anon();
                DEBUG("Set module from " << lex.parse_state().module->path() << " to " << local_mod->path());
                lex.parse_state().module = local_mod.get();
            }
            Parse_Mod_Item(lex, *local_mod, mv$(item_attrs));
            return ExprNodeP();
        // 'const' - Check if the next token isn't a `{`, if so it's an item. Otherwise, fall through
        case TOK_RWORD_CONST:
            if (LOOK_AHEAD(lex) != TOK_BRACE_OPEN) {
                PUTBACK(tok, lex);
                if (!local_mod) {
                    local_mod = lex.parse_state().get_current_mod().add_anon();
                    DEBUG("Set module from " << lex.parse_state().module->path() << " to " << local_mod->path());
                    lex.parse_state().module = local_mod.get();
                }
                Parse_Mod_Item(lex, *local_mod, mv$(item_attrs));
                return ExprNodeP();
            }
            break;
        // 'unsafe' - Check if the next token isn't a `{`, if so it's an item. Otherwise, fall through
        case TOK_RWORD_UNSAFE:
            if (LOOK_AHEAD(lex) != TOK_BRACE_OPEN) {
                PUTBACK(tok, lex);
                if (!local_mod) {
                    local_mod = lex.parse_state().get_current_mod().add_anon();
                    DEBUG("Set module from " << lex.parse_state().module->path() << " to " << local_mod->path());
                    lex.parse_state().module = local_mod.get();
                }
                Parse_Mod_Item(lex, *local_mod, mv$(item_attrs));
                return ExprNodeP();
            }
            // fall
        default:
            break;
    }
    PUTBACK(tok, lex);
    auto rv = Parse_ExprBlockLine(lex, &add_silence_if_end);
    if (rv) {
        rv->set_attrs(mv$(item_attrs));
    } else if (item_attrs.m_items.size() > 0) {
        // TODO: Is this an error? - Attributes on a expression that didn't yeild a node.
        // - They should have applied to the item that was parsed?
    } else {
    }
    return rv;
}

/// Parse a single line from a block
///
/// Handles:
/// - Block-level constructs (with lifetime annotations)
/// - use/extern/const/let
ExprNodeP Parse_ExprBlockLine(TokenStream& lex, bool* add_silence) {
    TRACE_FUNCTION;
    Token tok;
    ExprNodeP ret;
    bool add_silence_ignored = false;
    if (!add_silence) {
        add_silence = &add_silence_ignored;
    }

    if (GET_TOK(tok, lex) == TOK_LIFETIME) {
        // Lifetimes can only precede loops... and blocks?
        auto lifetime = tok.ident();
        GET_CHECK_TOK(tok, lex, TOK_COLON);

        switch (GET_TOK(tok, lex)) {
            case TOK_RWORD_LOOP:
                return NEWNODE(AST::ExprNode_Loop, lifetime, Parse_ExprBlockNode(lex));
            case TOK_RWORD_WHILE:
                return Parse_WhileStmt(lex, lifetime);
            case TOK_RWORD_FOR:
                return Parse_ForStmt(lex, lifetime);
            // NOTE: 1.39's libsyntax uses labelled block
            case TOK_BRACE_OPEN:
                PUTBACK(tok, lex);
                ret = Parse_ExprBlockNode(lex, /*is_unsafe*/ AST::ExprNode_Block::Type::Bare, lifetime);
                return ret;
            case TOK_RWORD_UNSAFE:
                ret = Parse_ExprBlockNode(lex, /*is_unsafe*/ AST::ExprNode_Block::Type::Unsafe, lifetime);
                return ret;
            case TOK_RWORD_CONST:
                ret = Parse_ExprBlockNode(lex, /*is_unsafe*/ AST::ExprNode_Block::Type::Const, lifetime);
                return ret;
                // TODO: Can these have labels?
                //case TOK_RWORD_IF:
                //    return Parse_IfStmt(lex);
                //case TOK_RWORD_MATCH:
                //    return Parse_Expr_Match(lex);

            default:
                throw ParseError::Unexpected(lex, tok);
        }
    } else {
        // HACK: Parse a path and look for a `macro::path! { }`, so it can be parsed as a block (instead of as an expression)
        // NOTE: This means that here is where the path parsing code ends up
        switch (tok.type()) {
            case TOK_IDENT:
            case TOK_RWORD_CRATE:
            case TOK_RWORD_SUPER:
            case TOK_DOUBLE_COLON:
            case TOK_RWORD_SELF:
                if (tok.type() != TOK_RWORD_SELF || lex.lookahead(0) == TOK_DOUBLE_COLON) {
                    PUTBACK(tok, lex);
                    auto p = Parse_Path(lex, PATH_GENERIC_EXPR);
                    if (lex.lookahead(0) == TOK_EXCLAM && lex.lookahead(1) == TOK_BRACE_OPEN) {
                        GET_CHECK_TOK(tok, lex, TOK_EXCLAM);
                        auto rv = Parse_ExprMacro(lex, std::move(p));
                        // If the block is followed by `.` or `?`, it's actually an expression!
                        if (lex.lookahead(0) == TOK_DOT || lex.lookahead(0) == TOK_QMARK) {
                            lex.putback(Token(Token::TagTakeIP(), InterpolatedFragment(InterpolatedFragment::EXPR, rv.release())));
                            return Parse_ExprBlockLine_Stmt(lex, *add_silence);
                        }
                        return rv;
                    }
                    tok = Token(Token::TagTakeIP(), InterpolatedFragment(std::move(p)));
                }
                break;
            default:
                break;
        }

        switch (tok.type()) {
            case TOK_INTERPOLATED_BLOCK:
                return tok.take_frag_node();
            case TOK_SEMICOLON:
                // Return a NULL expression, nothing here.
                return nullptr;

            // let binding
            case TOK_RWORD_LET:
                ret = Parse_Stmt_Let(lex);
                GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
                return ret;

            // Blocks that don't need semicolons
            // NOTE: If these are followed by a small set of tokens (`.` and `?`) then they are actually the start of an expression
            // HACK: Parse here, but if the next token is one of the set store in a TOK_INTERPOLATED_EXPR and invoke the statement parser
            case TOK_RWORD_LOOP: {
                ret = NEWNODE(AST::ExprNode_Loop, "", Parse_ExprBlockNode(lex));
                break;
            }
            case TOK_RWORD_WHILE:
                ret = Parse_WhileStmt(lex, Ident(""));
                break;
            case TOK_RWORD_FOR:
                ret = Parse_ForStmt(lex, Ident(""));
                break;
            case TOK_RWORD_IF:
                ret = Parse_IfStmt(lex);
                break;
            case TOK_RWORD_MATCH:
                ret = Parse_Expr_Match(lex);
                break;
            case TOK_RWORD_UNSAFE:
                ret = Parse_ExprBlockNode(lex, AST::ExprNode_Block::Type::Unsafe);
                break;
            case TOK_RWORD_CONST:
                ret = Parse_ExprBlockNode(lex, AST::ExprNode_Block::Type::Const);
                break;
            case TOK_BRACE_OPEN:
                PUTBACK(tok, lex);
                ret = Parse_ExprBlockNode(lex);
                break;

            // Flow control
            case TOK_RWORD_DO:
                // `do yeet`
            case TOK_RWORD_RETURN:
            case TOK_RWORD_YIELD:
            case TOK_RWORD_CONTINUE:
            case TOK_RWORD_BREAK: {
                PUTBACK(tok, lex);
                auto ret = Parse_Stmt(lex);
                if (LOOK_AHEAD(lex) == TOK_EOF) {
                } else if (GET_TOK(tok, lex) != TOK_SEMICOLON) {
                    CHECK_TOK(tok, TOK_BRACE_CLOSE);
                    PUTBACK(tok, lex);
                } else {
                    // return/continue/break don't need silencing
                }
                return ret;
            }
                // TODO: if this expression captures a block, then treat it as a statement.
                // Otherwise, interpret as normal expression
                // HACK: Just treat a leading `:expr` as a statement (rust-lang/rust #78829) (ref: rustc-1.39.0-src\vendor\indexmap\src\map.rs:1139)
                //case TOK_INTERPOLATED_EXPR:
                //    DEBUG(":expr");
                //    if( dynamic_cast<AST::ExprNode_Block .frag_node()
                //    PUTBACK(tok, lex);
                //    return Parse_Stmt(lex);

            default:
                PUTBACK(tok, lex);
                return Parse_ExprBlockLine_Stmt(lex, *add_silence);
        }

        // If the block is followed by `.` or `?`, it's actually an expression!
        if (lex.lookahead(0) == TOK_DOT || lex.lookahead(0) == TOK_QMARK) {
            lex.putback(Token(Token::TagTakeIP(), InterpolatedFragment(InterpolatedFragment::EXPR, ret.release())));
            return Parse_ExprBlockLine_Stmt(lex, *add_silence);
        }

        if (LOOK_AHEAD(lex) == TOK_SEMICOLON) {
            GET_TOK(tok, lex);
            *add_silence = true;
        }

        return ret;
    }
}

ExprNodeP Parse_ExprBlockLine_Stmt(TokenStream& lex, bool& has_semicolon) {
    Token tok;

    bool is_paren = lex.lookahead(0) == TOK_PAREN_OPEN;

    auto ret = Parse_Stmt(lex);

    // If `ret` is a braced macro call, don't require the semicolon (to remove the hackiness above)
    // - Don't trigger this when parens are present
    if (const auto* mac = dynamic_cast<AST::ExprNode_Macro*>(&*ret)) {
        if (!is_paren && mac->m_is_braced) {
            return ret;
        }
    }

    // If this expression statement wasn't followed by a semicolon, then it's yielding its value out of the block.
    // - I.e. The block should be ending
    if (!lex.getTokenIf(TOK_SEMICOLON)) {
        // - Allow TOK_EOF for macro expansion.
        switch (lex.lookahead(0)) {
            case TOK_EOF:
            case TOK_BRACE_CLOSE:
            case TOK_HASH: // Hack, some crates have `#[cfg()] foo #[cfg()] bar`
                break;
            default:
                // Force an error
                GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);
                break;
        }
    } else {
        has_semicolon = true;
    }
    return ret;
}

std::vector<AST::IfLet_Condition> Parse_IfLetChain(TokenStream& lex) {
    Token tok;
    std::vector<AST::IfLet_Condition> conditions;
    bool had_pat = false;
    do {
        if (lex.getTokenIf(TOK_RWORD_LET)) {
            lex.getTokenIf(TOK_PIPE);
            auto pat = Parse_Pattern(lex, AllowOrPattern::Yes);
            GET_CHECK_TOK(tok, lex, TOK_EQUAL);
            ExprNodeP val;
            {
                SET_PARSE_FLAG(lex, disallow_struct_literal);
                val = Parse_Expr3(lex); // This is just after `||` and `&&`
            }
            conditions.push_back(AST::IfLet_Condition{box$(pat), std::move(val)});
            had_pat = true;
        } else {
            ExprNodeP val;
            {
                SET_PARSE_FLAG(lex, disallow_struct_literal);
                val = Parse_Expr3(lex); // This is just after `||` and `&&`
            }

            // Chain boolean expressions to simplify downstream representation
            if (conditions.size() > 0 && !conditions.back().opt_pat) {
                conditions.back().value = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::BOOLAND, ::std::move(conditions.back().value), ::std::move(val));
            } else {
                conditions.push_back(AST::IfLet_Condition{std::unique_ptr<AST::Pattern>(), std::move(val)});
            }
        }
    } while (lex.getTokenIf(TOK_DOUBLE_AMP));

    if (lex.lookahead(0) == TOK_DOUBLE_PIPE) {
        if (had_pat) {
            TODO(lex.point_span(), "lazy boolean or in let chains not yet implemented (not yet valid rust, at 1.75)");
        } else {
            // Fall back to parsing as a standard expression
            auto prev = ::std::move(conditions[0].value);
            for (size_t i = 1; i < conditions.size(); i++) {
                prev = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::BOOLAND, ::std::move(prev), ::std::move(conditions[i].value));
            }
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_PIPE);
            SET_PARSE_FLAG(lex, disallow_struct_literal);
            auto n = Parse_Expr1_5(lex); // Boolean or
            auto rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::BOOLOR, ::std::move(prev), ::std::move(n));

            conditions.clear();
            conditions.push_back(AST::IfLet_Condition{std::unique_ptr<AST::Pattern>(), std::move(rv)});
        }
    }

    return conditions;
}

/// While loop (either as a statement, or as part of an expression)
ExprNodeP Parse_WhileStmt(TokenStream& lex, Ident lifetime) {
    auto conditions = Parse_IfLetChain(lex);
    return NEWNODE(AST::ExprNode_While, lifetime, ::std::move(conditions), Parse_ExprBlockNode(lex));
}

/// For loop (either as a statement, or as part of an expression)
ExprNodeP Parse_ForStmt(TokenStream& lex, Ident lifetime) {
    CLEAR_PARSE_FLAGS_EXPR(lex);
    Token tok;

    // Irrefutable pattern
    auto pat = Parse_Pattern(lex, AllowOrPattern::Yes);
    GET_CHECK_TOK(tok, lex, TOK_RWORD_IN);
    ExprNodeP val;
    {
        SET_PARSE_FLAG(lex, disallow_struct_literal);
        val = Parse_Expr0(lex);
    }
    return NEWNODE(AST::ExprNode_For, lifetime, ::std::move(pat), ::std::move(val), Parse_ExprBlockNode(lex));
}

/// Parse an 'if' statement
// Note: TOK_RWORD_IF has already been eaten
ExprNodeP Parse_IfStmt(TokenStream& lex) {
    TRACE_FUNCTION;

    Token tok;
    std::vector<AST::ExprNode_If::Arm> arms;
    ExprNodeP else_block;
    do {
        std::vector<AST::IfLet_Condition> conditions;

        {
            SET_PARSE_FLAG(lex, disallow_struct_literal);
            conditions = Parse_IfLetChain(lex);
        }

        // Contents
        ExprNodeP code = Parse_ExprBlockNode(lex);

        arms.push_back(AST::ExprNode_If::Arm{std::move(conditions), std::move(code)});

        // Handle else:
        if (!lex.getTokenIf(TOK_RWORD_ELSE)) {
            // No `else`, leave `else_block` as `nullptr`
            break;
        }
        // Recurse for 'else if'
        if (!lex.getTokenIf(TOK_RWORD_IF)) {
            else_block = Parse_ExprBlockNode(lex);
            break;
        }
        // Keep looping
    } while (true);

    return NEWNODE(AST::ExprNode_If, ::std::move(arms), ::std::move(else_block));
}

/// "match" block
ExprNodeP Parse_Expr_Match(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;
    AST::AttributeList node_attrs;

    CLEAR_PARSE_FLAGS_EXPR(lex);
    // 1. Get expression
    ExprNodeP switch_val;
    {
        SET_PARSE_FLAG(lex, disallow_struct_literal);
        switch_val = Parse_Expr1(lex);
    }
    //ASSERT(lex, !CHECK_PARSE_FLAG(lex, disallow_struct_literal) );
    GET_CHECK_TOK(tok, lex, TOK_BRACE_OPEN);

    ::std::vector<AST::ExprNode_Match_Arm> arms;
    do {
        if (lex.getTokenIf(TOK_BRACE_CLOSE, tok)) {
            break;
        }
        AST::ExprNode_Match_Arm arm;

        Parse_ParentAttrs(lex, node_attrs);
        arm.m_attrs = Parse_ItemAttrs(lex);

        // HACK: Questionably valid, but 1.29 librustc/hir/lowering.rs needs this
        lex.getTokenIf(TOK_PIPE);

        do {
            // Refutable pattern
            arm.m_patterns.push_back(Parse_Pattern(lex, AllowOrPattern::No));
        } while (GET_TOK(tok, lex) == TOK_PIPE);

        if (tok.type() == TOK_RWORD_IF) {
            arm.m_guard = Parse_IfLetChain(lex);
            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_FATARROW);

        arm.m_code = Parse_Stmt(lex);

        arms.push_back(::std::move(arm));

        // Match arms don't need a trailing comma (TODO: Only if braced)
        lex.getTokenIf(TOK_COMMA);
    } while (1);
    CHECK_TOK(tok, TOK_BRACE_CLOSE);

    auto rv = NEWNODE(AST::ExprNode_Match, ::std::move(switch_val), ::std::move(arms));
    rv->set_attrs(std::move(node_attrs));
    return rv;
}

/// "do catch" block
ExprNodeP Parse_Expr_Try(TokenStream& lex) {
    TRACE_FUNCTION;
    //Token   tok;

    auto inner = Parse_ExprBlockNode(lex);
    //TODO(lex.point_span(), "do catch");
    return NEWNODE(AST::ExprNode_Try, ::std::move(inner));
}

ExprNodeP Parse_FlowControl(TokenStream& lex, AST::ExprNode_Flow::Type type) {
    Token tok;
    Ident lifetime = Ident("");
    // continue/break can specify a target
    if (type == AST::ExprNode_Flow::CONTINUE || type == AST::ExprNode_Flow::BREAK) {
        if (lex.lookahead(0) == TOK_LIFETIME) {
            GET_TOK(tok, lex);
            lifetime = tok.ident();
        }
    }
    // Return value
    // TODO: Should this prevent `continue value;`?
    ExprNodeP val;
    switch (LOOK_AHEAD(lex)) {
        case TOK_EOF:
        case TOK_SEMICOLON:
        case TOK_COMMA:
        case TOK_BRACE_CLOSE:
        case TOK_PAREN_CLOSE:
        case TOK_SQUARE_CLOSE:
            break;
        default:
            val = Parse_Expr0(lex);
            break;
    }
    return NEWNODE(AST::ExprNode_Flow, type, std::move(lifetime), ::std::move(val));
}

/// Parses the 'stmt' fragment specifier
/// - Flow control
/// - Expressions
ExprNodeP Parse_Stmt(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    switch (GET_TOK(tok, lex)) {
        case TOK_INTERPOLATED_STMT:
            return tok.take_frag_node();
        // Duplicated here for the :stmt pattern fragment.
        case TOK_RWORD_LET:
            return Parse_Stmt_Let(lex);
        case TOK_RWORD_YIELD:
            return Parse_FlowControl(lex, AST::ExprNode_Flow::YIELD);
        case TOK_RWORD_CONTINUE:
            return Parse_FlowControl(lex, AST::ExprNode_Flow::CONTINUE);
        case TOK_RWORD_BREAK:
            return Parse_FlowControl(lex, AST::ExprNode_Flow::BREAK);
        case TOK_RWORD_RETURN:
            return Parse_FlowControl(lex, AST::ExprNode_Flow::RETURN);
        case TOK_BRACE_OPEN:
            PUTBACK(tok, lex);
            return Parse_ExprBlockNode(lex);
        case TOK_RWORD_IF:
        case TOK_RWORD_WHILE:
        case TOK_RWORD_FOR:
        case TOK_RWORD_LOOP:
        case TOK_RWORD_MATCH: {
            PUTBACK(tok, lex);
            SET_PARSE_FLAG(lex, disallow_call_or_index);
            return Parse_ExprFC(lex);
        }
        //case TOK_RWORD_DO:
        //    if( lex.lookahead(0) == "yeet" ) {
        //        return Parse_FlowControl(lex, AST::ExprNode_Flow::YEET);
        //    }
        default:
            PUTBACK(tok, lex);
            return Parse_Expr0(lex);
    }
}

ExprNodeP Parse_Stmt_Let(TokenStream& lex) {
    Token tok;
    AST::Pattern pat = Parse_Pattern(lex, AllowOrPattern::Yes); // irrefutable
    TypeRef type{lex.point_span()};
    if (lex.getTokenIf(TOK_COLON)) {
        type = Parse_Type(lex);
    }
    ExprNodeP val;
    ExprNodeP else_arm;
    if (lex.getTokenIf(TOK_EQUAL)) {
        val = Parse_Expr0(lex);
        if (lex.getTokenIf(TOK_RWORD_ELSE)) {
            else_arm = Parse_ExprBlockNode(lex);
        }
    }
    return NEWNODE(AST::ExprNode_LetBinding, ::std::move(pat), mv$(type), ::std::move(val), ::std::move(else_arm));
}

::std::vector<ExprNodeP> Parse_ParenList(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    CLEAR_PARSE_FLAGS_EXPR(lex);

    ::std::vector<ExprNodeP> rv;
    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    if (!lex.getTokenIf(TOK_PAREN_CLOSE)) {
        do {
            if (lex.getTokenIf(TOK_PAREN_CLOSE, tok)) {
                break;
            }
            rv.push_back(Parse_Expr0(lex));
        } while (GET_TOK(tok, lex) == TOK_COMMA);
        CHECK_TOK(tok, TOK_PAREN_CLOSE);
    }
    return rv;
}

// 0: Assign
ExprNodeP Parse_Expr0(TokenStream& lex) {
    //TRACE_FUNCTION;
    Token tok;

    CLEAR_PARSE_FLAG(lex, disallow_call_or_index);

    auto expr_attrs = Parse_ItemAttrs(lex);

    ExprNodeP rv = Parse_Expr1(lex);
    auto op = AST::ExprNode_Assign::NONE;
    switch (GET_TOK(tok, lex)) {
        case TOK_PLUS_EQUAL:
            op = AST::ExprNode_Assign::ADD;
            if (0) {
                case TOK_DASH_EQUAL:
                    op = AST::ExprNode_Assign::SUB;
            }
            if (0) {
                case TOK_STAR_EQUAL:
                    op = AST::ExprNode_Assign::MUL;
            }
            if (0) {
                case TOK_SLASH_EQUAL:
                    op = AST::ExprNode_Assign::DIV;
            }
            if (0) {
                case TOK_PERCENT_EQUAL:
                    op = AST::ExprNode_Assign::MOD;
            }
            if (0) {
                case TOK_AMP_EQUAL:
                    op = AST::ExprNode_Assign::AND;
            }
            if (0) {
                case TOK_PIPE_EQUAL:
                    op = AST::ExprNode_Assign::OR;
            }
            if (0) {
                case TOK_CARET_EQUAL:
                    op = AST::ExprNode_Assign::XOR;
            }
            if (0) {
                case TOK_DOUBLE_GT_EQUAL:
                    op = AST::ExprNode_Assign::SHR;
            }
            if (0) {
                case TOK_DOUBLE_LT_EQUAL:
                    op = AST::ExprNode_Assign::SHL;
            }
            if (0) {
                case TOK_EQUAL:
                    op = AST::ExprNode_Assign::NONE;
            }
            rv = NEWNODE(AST::ExprNode_Assign, op, ::std::move(rv), Parse_Expr0(lex));
            rv->set_attrs(mv$(expr_attrs));
            return rv;

        default:
            PUTBACK(tok, lex);
            rv->set_attrs(mv$(expr_attrs));
            return rv;
    }
}

#define LEFTASSOC(cur, _next, cases)                 \
    ExprNodeP _next(TokenStream& lex);               \
    ExprNodeP cur(TokenStream& lex) {                \
        ExprNodeP (*next)(TokenStream&) = _next;     \
        ExprNodeP rv = next(lex);                    \
        while (true) {                               \
            Token tok;                               \
            switch ((tok = lex.getToken()).type()) { \
                cases \
        default: \
            /*::std::cout << "<<" << #cur << ::std::endl; */\
            PUTBACK(tok, lex);                              \
                return rv;                           \
            }                                        \
        }                                            \
    }

bool Parse_IsTokValue(eTokenType tok_type) {
    switch (tok_type) {
        case TOK_DOUBLE_COLON:
        case TOK_DOUBLE_LT:
        case TOK_LT:
        case TOK_IDENT:
        case TOK_INTEGER:
        case TOK_FLOAT:
        case TOK_STRING:
        case TOK_CSTRING:
        case TOK_BYTESTRING:
        case TOK_UNDERSCORE:
        case TOK_RWORD_TRUE:
        case TOK_RWORD_FALSE:
        case TOK_RWORD_SELF:
        case TOK_RWORD_SUPER:
        case TOK_RWORD_CRATE:
        case TOK_RWORD_BOX:
        case TOK_RWORD_IN:
        case TOK_PAREN_OPEN:
        case TOK_SQUARE_OPEN:

        case TOK_RWORD_MATCH:
        case TOK_RWORD_IF:

        case TOK_INTERPOLATED_PATH:
        case TOK_INTERPOLATED_EXPR:

        case TOK_PIPE:
        case TOK_EXCLAM:
        case TOK_DASH:
        case TOK_STAR:
        case TOK_AMP:
            return true;
        default:
            return false;
    }
}

ExprNodeP Parse_Expr1_1(TokenStream& lex);

ExprNodeP Parse_Expr1(TokenStream& lex) {
    Token tok;
    ExprNodeP (*next)(TokenStream&) = Parse_Expr1_1;

    auto dest = next(lex);
    if (lex.lookahead(0) == TOK_THINARROW_LEFT) {
        GET_TOK(tok, lex);
        auto val = Parse_Expr1(lex);
        return NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::PLACE_IN, mv$(dest), mv$(val));
    } else {
        return dest;
    }
}

ExprNodeP Parse_Expr1_2(TokenStream& lex);

// Very evil handling for '..'
ExprNodeP Parse_Expr1_1(TokenStream& lex) {
    Token tok;
    ExprNodeP (*next)(TokenStream&) = Parse_Expr1_2;
    ExprNodeP left, right;

    // Inclusive range to a value
    if (GET_TOK(tok, lex) == TOK_TRIPLE_DOT || tok.type() == TOK_DOUBLE_DOT_EQUAL) {
        right = next(lex);
        return NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::RANGE_INC, nullptr, mv$(right));
    } else {
        PUTBACK(tok, lex);
    }

    // Exclusive ranges
    // - If NOT `.. <VAL>`, parse a leading value
    if (!lex.getTokenIf(TOK_DOUBLE_DOT, tok)) {
        left = next(lex);

        // - If NOT `<VAL> ..`, return the value
        if (!lex.getTokenIf(TOK_DOUBLE_DOT, tok)) {
            return ::std::move(left);
        }
    }
    assert(tok.type() == TOK_DOUBLE_DOT);
    // If the next token is part of a value, parse that value
    if (Parse_IsTokValue(LOOK_AHEAD(lex))) {
        right = next(lex);
    } else {
        // Otherwise, leave `right` as nullptr
    }

    return NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::RANGE, ::std::move(left), ::std::move(right));
}
LEFTASSOC(Parse_Expr1_2, Parse_Expr1_5,
    case TOK_TRIPLE_DOT:
        rv = NEWNODE( AST::ExprNode_BinOp, AST::ExprNode_BinOp::RANGE_INC, mv$(rv), next(lex) );
        break;
    case TOK_DOUBLE_DOT_EQUAL:
        rv = NEWNODE( AST::ExprNode_BinOp, AST::ExprNode_BinOp::RANGE_INC, mv$(rv), next(lex) );
        break;
)
// 1: Bool OR
LEFTASSOC(Parse_Expr1_5, Parse_Expr2, case TOK_DOUBLE_PIPE : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::BOOLOR, ::std::move(rv), next(lex)); break;)
// 2: Bool AND
LEFTASSOC(Parse_Expr2, Parse_Expr3, case TOK_DOUBLE_AMP : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::BOOLAND, ::std::move(rv), next(lex)); break;)
// 3: (In)Equality
LEFTASSOC(Parse_Expr3, Parse_Expr4, case TOK_DOUBLE_EQUAL : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::CMPEQU, ::std::move(rv), next(lex)); break; case TOK_EXCLAM_EQUAL : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::CMPNEQU, ::std::move(rv), next(lex)); break;)
// 4: Comparisons
LEFTASSOC(Parse_Expr4, Parse_Expr5, case TOK_LT : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::CMPLT, ::std::move(rv), next(lex)); break; case TOK_GT : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::CMPGT, ::std::move(rv), next(lex)); break; case TOK_LTE : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::CMPLTE, ::std::move(rv), next(lex)); break; case TOK_GTE : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::CMPGTE, ::std::move(rv), next(lex)); break;)
// 5: Bit OR
LEFTASSOC(Parse_Expr5, Parse_Expr6, case TOK_PIPE : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::BITOR, ::std::move(rv), next(lex)); break;)
// 6: Bit XOR
LEFTASSOC(Parse_Expr6, Parse_Expr7, case TOK_CARET : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::BITXOR, ::std::move(rv), next(lex)); break;)
// 7: Bit AND
LEFTASSOC(Parse_Expr7, Parse_Expr8, case TOK_AMP : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::BITAND, ::std::move(rv), next(lex)); break;)
// 8: Bit Shifts
LEFTASSOC(Parse_Expr8, Parse_Expr9, case TOK_DOUBLE_LT : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::SHL, ::std::move(rv), next(lex)); break; case TOK_DOUBLE_GT : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::SHR, ::std::move(rv), next(lex)); break;)
// 9: Add / Subtract
LEFTASSOC(Parse_Expr9, Parse_Expr10, case TOK_PLUS : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::ADD, ::std::move(rv), next(lex)); break; case TOK_DASH : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::SUB, ::std::move(rv), next(lex)); break;)
// 10: Times / Divide / Modulo
LEFTASSOC(Parse_Expr10, Parse_Expr11, case TOK_STAR : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::MULTIPLY, ::std::move(rv), next(lex)); break; case TOK_SLASH : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::DIVIDE, ::std::move(rv), next(lex)); break; case TOK_PERCENT : rv = NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::MODULO, ::std::move(rv), next(lex)); break;)
// 11: Cast
LEFTASSOC(Parse_Expr11, Parse_Expr12, case TOK_RWORD_AS : rv = NEWNODE(AST::ExprNode_Cast, ::std::move(rv), Parse_Type(lex, false)); break;)
// 12: Type Ascription
ExprNodeP Parse_Expr13(TokenStream& lex);

ExprNodeP Parse_Expr12(TokenStream& lex) {
    Token tok;
    auto rv = Parse_Expr13(lex);
    if (lex.getTokenIf(TOK_COLON)) {
        rv = NEWNODE(AST::ExprNode_TypeAnnotation, mv$(rv), Parse_Type(lex));
    }
    return rv;
}

// 13: Unaries
ExprNodeP Parse_Expr13(TokenStream& lex) {
    Token tok;
    switch (GET_TOK(tok, lex)) {
        case TOK_DASH:
            return NEWNODE(AST::ExprNode_UniOp, AST::ExprNode_UniOp::NEGATE, Parse_Expr12(lex));
        case TOK_EXCLAM:
            return NEWNODE(AST::ExprNode_UniOp, AST::ExprNode_UniOp::INVERT, Parse_Expr12(lex));
        case TOK_STAR:
            return NEWNODE(AST::ExprNode_Deref, Parse_Expr12(lex));
        case TOK_RWORD_BOX:
            return NEWNODE(AST::ExprNode_UniOp, AST::ExprNode_UniOp::BOX, Parse_Expr12(lex));
        case TOK_RWORD_IN: {
            ExprNodeP dest;
            {
                SET_PARSE_FLAG(lex, disallow_struct_literal);
                dest = Parse_Expr1(lex);
            }
            auto val = Parse_ExprBlockNode(lex);
            return NEWNODE(AST::ExprNode_BinOp, AST::ExprNode_BinOp::PLACE_IN, mv$(dest), mv$(val));
        }
        case TOK_DOUBLE_AMP:
            // HACK: Split && into & &
            lex.putback(Token(TOK_AMP));
        case TOK_AMP:
            if (lex.lookahead(0) == TOK_IDENT) {
                GET_TOK(tok, lex);
                if (tok.ident() == "raw") {
                    if (lex.lookahead(0) == TOK_RWORD_MUT) {
                        GET_TOK(tok, lex);
                        return NEWNODE(AST::ExprNode_UniOp, AST::ExprNode_UniOp::RawBorrowMut, Parse_Expr12(lex));
                    } else if (lex.lookahead(0) == TOK_RWORD_CONST) {
                        GET_TOK(tok, lex);
                        return NEWNODE(AST::ExprNode_UniOp, AST::ExprNode_UniOp::RawBorrow, Parse_Expr12(lex));
                    } else {
                    }
                }
                PUTBACK(tok, lex);
            }
            if (lex.getTokenIf(TOK_RWORD_MUT)) {
                return NEWNODE(AST::ExprNode_UniOp, AST::ExprNode_UniOp::REFMUT, Parse_Expr12(lex));
            } else {
                return NEWNODE(AST::ExprNode_UniOp, AST::ExprNode_UniOp::REF, Parse_Expr12(lex));
            }
        default:
            PUTBACK(tok, lex);
            return Parse_ExprFC(lex);
    }
}

ExprNodeP Parse_ExprVal(TokenStream& lex);

ExprNodeP Parse_ExprFC(TokenStream& lex) {
    ExprNodeP val = Parse_ExprVal(lex);
    while (true) {
        Token tok;
        switch (GET_TOK(tok, lex)) {
            // Expression method call
            case TOK_PAREN_OPEN:
                if (CHECK_PARSE_FLAG(lex, disallow_call_or_index)) {
                    PUTBACK(tok, lex);
                    return val;
                }
                PUTBACK(tok, lex);
                val = NEWNODE(AST::ExprNode_CallObject, ::std::move(val), Parse_ParenList(lex));
                break;
            case TOK_SQUARE_OPEN:
                if (CHECK_PARSE_FLAG(lex, disallow_call_or_index)) {
                    PUTBACK(tok, lex);
                    return val;
                }
                val = NEWNODE(AST::ExprNode_Index, ::std::move(val), Parse_Expr0(lex));
                GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                break;

            case TOK_QMARK:
                val = NEWNODE(AST::ExprNode_UniOp, AST::ExprNode_UniOp::QMARK, mv$(val));
                break;

            case TOK_DOT:
                // Field access / method call / tuple index
                switch (GET_TOK(tok, lex)) {
                    case TOK_IDENT: {
                        AST::PathNode pn(tok.ident().name, {});
                        switch (GET_TOK(tok, lex)) {
                            case TOK_PAREN_OPEN:
                                PUTBACK(tok, lex);
                                val = NEWNODE(AST::ExprNode_CallMethod, ::std::move(val), ::std::move(pn), Parse_ParenList(lex));
                                break;
                            case TOK_DOUBLE_COLON:
                                if (lex.getTokenIf(TOK_DOUBLE_LT)) {
                                    lex.putback(Token(TOK_LT));
                                } else {
                                    GET_CHECK_TOK(tok, lex, TOK_LT);
                                }
                                pn.args() = Parse_Path_GenericList(lex);
                                val = NEWNODE(AST::ExprNode_CallMethod, ::std::move(val), ::std::move(pn), Parse_ParenList(lex));
                                break;
                            default:
                                val = NEWNODE(AST::ExprNode_Field, ::std::move(val), pn.name());
                                PUTBACK(tok, lex);
                                break;
                        }
                        break;
                    }
                    case TOK_INTEGER:
                        val = NEWNODE(AST::ExprNode_Field, ::std::move(val), RcString::new_interned(FMT(tok.intval())));
                        break;
                    case TOK_RWORD_AWAIT:
                        val = NEWNODE(AST::ExprNode_UniOp, AST::ExprNode_UniOp::AWait, ::std::move(val));
                        break;
                    default:
                        throw ParseError::Unexpected(lex, mv$(tok));
                }
                break;
            default:
                PUTBACK(tok, lex);
                return val;
        }
    }
}

ExprNodeP Parse_ExprVal_StructLiteral(TokenStream& lex, AST::Path path) {
    TRACE_FUNCTION;
    Token tok;

    // #![feature(relaxed_adts)]
    if (LOOK_AHEAD(lex) == TOK_INTEGER) {
        ::std::map<unsigned int, ExprNodeP> nodes;
        while (GET_TOK(tok, lex) == TOK_INTEGER) {
            unsigned int ofs = static_cast<unsigned int>(tok.intval().truncate_u64());
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            ExprNodeP val = Parse_Stmt(lex);
            if (!nodes.insert(::std::make_pair(ofs, mv$(val))).second) {
                ERROR(lex.point_span(), E0000, "Duplicate index");
            }

            if (GET_TOK(tok, lex) == TOK_BRACE_CLOSE) {
                break;
            }
            CHECK_TOK(tok, TOK_COMMA);
        }
        CHECK_TOK(tok, TOK_BRACE_CLOSE);

        ::std::vector<ExprNodeP> items;
        unsigned int i = 0;
        for (auto& p : nodes) {
            if (p.first != i) {
                ERROR(lex.point_span(), E0000, "Missing index " << i);
            }
            items.push_back(mv$(p.second));
            i++;
        }

        return NEWNODE(AST::ExprNode_CallPath, mv$(path), mv$(items));
    }

    // Braced structure literal
    // - A series of 0 or more pairs of <ident>: <expr>,
    // - '..' <expr>
    ::AST::ExprNode_StructLiteral::t_values items;
    while (GET_TOK(tok, lex) == TOK_IDENT || tok.type() == TOK_HASH) {
        ::AST::AttributeList attrs; // Note: Parse_ItemAttrs uses lookahead, so can't use it here.
        if (tok.type() == TOK_HASH) {
            PUTBACK(tok, lex);
            attrs = Parse_ItemAttrs(lex);
            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_IDENT);
        auto h = tok.ident().hygiene;
        auto name = tok.ident().name;

        ExprNodeP val;
        if (lex.lookahead(0) != TOK_COLON) {
            val = NEWNODE(AST::ExprNode_NamedValue, ::AST::Path::new_relative(h, {::AST::PathNode(name)}));
        } else {
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            val = Parse_Expr0(lex);
        }
        items.push_back(::AST::ExprNode_StructLiteral::Ent{mv$(attrs), mv$(name), mv$(val)});

        if (GET_TOK(tok, lex) == TOK_BRACE_CLOSE) {
            break;
        }
        CHECK_TOK(tok, TOK_COMMA);
    }
    ExprNodeP base_val;
    if (tok.type() == TOK_DOUBLE_DOT) {
        if (lex.getTokenIf(TOK_BRACE_CLOSE)) {
            return NEWNODE(AST::ExprNode_StructLiteralPattern, path, ::std::move(items));
        } else {
            // default
            base_val = Parse_Expr0(lex);
        }
        GET_TOK(tok, lex);
    }
    CHECK_TOK(tok, TOK_BRACE_CLOSE);

    return NEWNODE(AST::ExprNode_StructLiteral, path, ::std::move(base_val), ::std::move(items));
}

ExprNodeP Parse_ExprVal_Closure(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    GET_TOK(tok, lex);

    // [`static`]
    bool is_immovable = false;
    if (tok == TOK_RWORD_STATIC) {
        GET_TOK(tok, lex);
        is_immovable = true;
    }

    // [`move`]
    bool is_move = false;
    if (tok == TOK_RWORD_MOVE) {
        GET_TOK(tok, lex);
        is_move = true;
    }

    ::std::vector<::std::pair<AST::Pattern, TypeRef>> args;
    if (tok == TOK_DOUBLE_PIPE) {
        // `||` - Empty argument list
    } else if (tok == TOK_PIPE) {
        // `|...|` - Arguments present
        while (!lex.getTokenIf(TOK_PIPE, tok)) {
            // Irrefutable pattern
            AST::Pattern pat = Parse_Pattern(lex, AllowOrPattern::No);

            TypeRef type{lex.point_span()};
            if (lex.getTokenIf(TOK_COLON)) {
                type = Parse_Type(lex);
            }

            args.push_back(::std::make_pair(::std::move(pat), ::std::move(type)));

            if (!lex.getTokenIf(TOK_COMMA)) {
                GET_TOK(tok, lex);
                break;
            }
        }
        CHECK_TOK(tok, TOK_PIPE);
    } else {
        throw ParseError::Unexpected(lex, tok, {TOK_PIPE, TOK_DOUBLE_PIPE, TOK_RWORD_MOVE, TOK_RWORD_STATIC});
    }

    auto rt = TypeRef(lex.point_span());
    if (lex.getTokenIf(TOK_THINARROW)) {
        rt = Parse_Type(lex);
    }

    auto code = Parse_Expr0(lex);

    return NEWNODE(AST::ExprNode_Closure, ::std::move(args), ::std::move(rt), ::std::move(code), is_move, is_immovable);
}

ExprNodeP Parse_ExprVal_Inner(TokenStream& lex) {
    TRACE_FUNCTION;

    Token tok;
    AST::Path path;

    if (lex.lookahead(0) == TOK_INTERPOLATED_PATH && ((lex.lookahead(1) == TOK_RWORD_MOVE && lex.lookahead(2) == TOK_BRACE_OPEN) || lex.lookahead(1) == TOK_BRACE_OPEN)) {
        GET_TOK(tok, lex);
        if (tok.frag_path().is_trivial() && tok.frag_path().as_trivial() == "gen") {
            // Generators!
            bool is_move = lex.getTokenIf(TOK_RWORD_MOVE);
            return NEWNODE(AST::ExprNode_GeneratorBlock, Parse_ExprBlockNode(lex, AST::ExprNode_Block::Type::Bare), is_move);
        }
        PUTBACK(tok, lex);
    }

    switch (GET_TOK(tok, lex)) {
        case TOK_BRACE_OPEN:
            PUTBACK(tok, lex);
            return Parse_ExprBlockNode(lex);

        case TOK_INTERPOLATED_EXPR:
        case TOK_INTERPOLATED_BLOCK:
            return tok.take_frag_node();

        // Return/break/continue/... also parsed here (but recurses back up to actually handle them)
        case TOK_RWORD_RETURN:
        case TOK_RWORD_YIELD:
        case TOK_RWORD_CONTINUE:
        case TOK_RWORD_BREAK:
            PUTBACK(tok, lex);
            return Parse_Stmt(lex);

        case TOK_LIFETIME:
            PUTBACK(tok, lex);
            return Parse_ExprBlockLine(lex, nullptr);

        case TOK_RWORD_LOOP:
            return NEWNODE(AST::ExprNode_Loop, "", Parse_ExprBlockNode(lex));
        case TOK_RWORD_WHILE:
            return Parse_WhileStmt(lex, Ident(""));
        case TOK_RWORD_FOR:
            return Parse_ForStmt(lex, Ident(""));
        case TOK_RWORD_TRY: // Only emitted in 2018
            return Parse_Expr_Try(lex);
        case TOK_RWORD_DO:
            GET_TOK(tok, lex);
            // `do catch` (1.29) - stabilised later as `try`
            if (tok.type() == TOK_IDENT && tok.ident().name == "catch") {
                return Parse_Expr_Try(lex);
            }
            // `do yeet` (1.74) - Not stabilised (as of 2024-04)
            else if (tok.type() == TOK_IDENT && tok.ident().name == "yeet") {
                return Parse_FlowControl(lex, AST::ExprNode_Flow::YEET);
            } else {
                throw ParseError::Unexpected(lex, tok);
            }
        case TOK_RWORD_MATCH:
            return Parse_Expr_Match(lex);
        case TOK_RWORD_IF:
            return Parse_IfStmt(lex);
        case TOK_RWORD_ASYNC: {
            bool is_move = lex.getTokenIf(TOK_RWORD_MOVE);
            return NEWNODE(AST::ExprNode_AsyncBlock, Parse_ExprBlockNode(lex, AST::ExprNode_Block::Type::Bare), is_move);
        }
        case TOK_RWORD_UNSAFE:
            return Parse_ExprBlockNode(lex, AST::ExprNode_Block::Type::Unsafe);
        case TOK_RWORD_CONST:
            return Parse_ExprBlockNode(lex, AST::ExprNode_Block::Type::Const);

        // Paths
        // `self` can be a value, or start a path
        case TOK_RWORD_SELF:
            if (LOOK_AHEAD(lex) != TOK_DOUBLE_COLON) {
                static const RcString rcstring_self = RcString::new_interned("self");
                return NEWNODE(AST::ExprNode_NamedValue, AST::Path(rcstring_self));
            }
            // Fall through to normal paths
        case TOK_DOUBLE_LT:
        case TOK_LT:
        case TOK_RWORD_CRATE:
        case TOK_RWORD_SUPER:
        case TOK_DOUBLE_COLON:
        case TOK_IDENT:
        case TOK_INTERPOLATED_PATH:
            PUTBACK(tok, lex);
            path = Parse_Path(lex, PATH_GENERIC_EXPR);

            DEBUG("path = " << path << ", lookahead=" << Token::typestr(lex.lookahead(0)));
            switch (GET_TOK(tok, lex)) {
                case TOK_EXCLAM:
                    return Parse_ExprMacro(lex, mv$(path));
                case TOK_PAREN_OPEN:
                    // Function call
                    PUTBACK(tok, lex);
                    return NEWNODE(AST::ExprNode_CallPath, ::std::move(path), Parse_ParenList(lex));
                case TOK_BRACE_OPEN:
                    if (!CHECK_PARSE_FLAG(lex, disallow_struct_literal)) {
                        return Parse_ExprVal_StructLiteral(lex, ::std::move(path));
                    } else {
                        DEBUG("Not parsing struct literal");
                    }
                    // Value
                    PUTBACK(tok, lex);
                    return NEWNODE(AST::ExprNode_NamedValue, ::std::move(path));
                // `builtin # <name>` - seems to be a 1.74 era hack to extend syntax
                // - Only `builtin # offset_of( <ty>, <field>[, <subfield>]... )` is implemented
                //   > mrustc translates this to an intrinsic call with the fields as string/integer arguments (simple to pass through)
                case TOK_HASH:
                    if (path.is_trivial() && path.as_trivial() == "builtin") {
                        GET_CHECK_TOK(tok, lex, TOK_IDENT);
                        if (tok.ident() == "offset_of") {
                            GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
                            auto ty = Parse_Type(lex);
                            std::vector<AST::ExprNodeP> args;
                            do {
                                GET_CHECK_TOK(tok, lex, TOK_COMMA);
                                if (lex.lookahead(0) == TOK_INTERPOLATED_EXPR) {
                                    GET_CHECK_TOK(tok, lex, TOK_INTERPOLATED_EXPR);
                                    const auto* expr = &tok.frag_node();
                                    std::vector<AST::ExprNodeP> expr_args;
                                    for (;;) {
                                        if (const auto* n = dynamic_cast<const AST::ExprNode_NamedValue*>(expr)) {
                                            expr_args.push_back(NEWNODE(AST::ExprNode_String, n->m_path.as_trivial().c_str(), {}));
                                            break;
                                        } else if (const auto* n = dynamic_cast<const AST::ExprNode_Integer*>(expr)) {
                                            expr_args.push_back(NEWNODE(AST::ExprNode_Integer, n->m_value, n->m_datatype));
                                            break;
                                        } else if (const auto* n = dynamic_cast<const AST::ExprNode_Float*>(expr)) {
                                            auto value = static_cast<_Float128>(n->m_value);
                                            if (value < 0 || __builtin_isnan(value) || __builtin_isinf(value)) {
                                                TODO(lex.point_span(), "offset_of - invalid tuple indices " << *expr);
                                            }
                                            auto whole = ::truncf128(value);
                                            if (whole > static_cast<_Float128>(SIZE_MAX)) {
                                                TODO(lex.point_span(), "offset_of - tuple index is too large " << *expr);
                                            }

                                            auto fraction = value - whole;
                                            _Float128 scale = 1;
                                            _Float128 fractional_index = 0;
                                            auto tolerance_per_unit = static_cast<_Float128>(parse_float_value("1e-32"));
                                            bool found_fractional_index = false;
                                            for (unsigned digits = 0; digits < 20; digits++) {
                                                scale *= 10;
                                                auto scaled = fraction * scale;
                                                fractional_index = ::roundf128(scaled);
                                                if (::fabsf128(scaled - fractional_index) <= scale * tolerance_per_unit) {
                                                    found_fractional_index = true;
                                                    break;
                                                }
                                            }
                                            if (!found_fractional_index || fractional_index > static_cast<_Float128>(SIZE_MAX)) {
                                                TODO(lex.point_span(), "offset_of - invalid tuple indices " << *expr);
                                            }
                                            expr_args.push_back(NEWNODE(AST::ExprNode_Integer, U128(static_cast<uint64_t>(fractional_index)), CORETYPE_ANY));
                                            expr_args.push_back(NEWNODE(AST::ExprNode_Integer, U128(static_cast<uint64_t>(whole)), CORETYPE_ANY));
                                            break;
                                        } else if (const auto* n = dynamic_cast<const AST::ExprNode_Field*>(expr)) {
                                            expr_args.push_back(NEWNODE(AST::ExprNode_String, n->m_name.c_str(), {}));
                                            expr = &*n->m_obj;
                                        } else {
                                            TODO(lex.point_span(), "offset_of - " << *expr);
                                        }
                                    }
                                    while (!expr_args.empty()) {
                                        args.push_back(std::move(expr_args.back()));
                                        expr_args.pop_back();
                                    }
                                } else if (lex.lookahead(0) == TOK_INTEGER) {
                                    GET_CHECK_TOK(tok, lex, TOK_INTEGER);
                                    args.push_back(NEWNODE(AST::ExprNode_Integer, tok.intval(), tok.datatype()));
                                } else {
                                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                                    args.push_back(NEWNODE(AST::ExprNode_String, tok.ident().name.c_str(), tok.ident().hygiene));
                                }
                            } while (lex.lookahead(0) == TOK_COMMA);
                            GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);

                            // TODO: How to emit this, maybe as a hacky intrinsic?
                            // ::"#intrinsics"::offset_of::<T>("field1",...)
                            // - Fiddly
                            path = AST::Path(RcString::new_interned("#intrinsics"), {AST::PathNode("offset_of")});
                            path.nodes().back().args().m_entries.push_back(std::move(ty));
                            return NEWNODE(AST::ExprNode_CallPath, std::move(path), std::move(args));
                        } else {
                            TODO(lex.point_span(), "`builtin #` support - " << tok.ident());
                        }
                    }
                default:
                    // Value
                    PUTBACK(tok, lex);
                    return NEWNODE(AST::ExprNode_NamedValue, ::std::move(path));
            }
        // Closures
        case TOK_RWORD_STATIC:
        case TOK_RWORD_MOVE:
        case TOK_PIPE:
        case TOK_DOUBLE_PIPE:
            PUTBACK(tok, lex);
            return Parse_ExprVal_Closure(lex);

        case TOK_UNDERSCORE:
            return NEWNODE(AST::ExprNode_WildcardPattern);
        case TOK_INTEGER:
            return NEWNODE(AST::ExprNode_Integer, tok.intval(), tok.datatype());
        case TOK_FLOAT:
            return NEWNODE(AST::ExprNode_Float, tok.floatval(), tok.datatype());
        case TOK_STRING:
            return NEWNODE(AST::ExprNode_String, tok.str(), tok.str_hygiene());
        case TOK_BYTESTRING:
            return NEWNODE(AST::ExprNode_ByteString, tok.str());
        case TOK_CSTRING:
            return NEWNODE(AST::ExprNode_CString, tok.str());
        case TOK_RWORD_TRUE:
            return NEWNODE(AST::ExprNode_Bool, true);
        case TOK_RWORD_FALSE:
            return NEWNODE(AST::ExprNode_Bool, false);
        case TOK_PAREN_OPEN:
            if (lex.getTokenIf(TOK_PAREN_CLOSE)) {
                DEBUG("Unit");
                return NEWNODE(AST::ExprNode_Tuple, ::std::vector<ExprNodeP>());
            } else {
                CLEAR_PARSE_FLAGS_EXPR(lex);

                ExprNodeP rv = Parse_Expr0(lex);
                if (GET_TOK(tok, lex) == TOK_COMMA) {
                    ::std::vector<ExprNodeP> ents;
                    ents.push_back(::std::move(rv));
                    do {
                        if (lex.getTokenIf(TOK_PAREN_CLOSE, tok)) {
                            break;
                        }
                        ents.push_back(Parse_Expr0(lex));
                    } while (GET_TOK(tok, lex) == TOK_COMMA);
                    rv = NEWNODE(AST::ExprNode_Tuple, ::std::move(ents));
                }
                CHECK_TOK(tok, TOK_PAREN_CLOSE);
                return rv;
            }
        case TOK_SQUARE_OPEN:
            if (lex.getTokenIf(TOK_SQUARE_CLOSE)) {
                // Empty literal
                return NEWNODE(AST::ExprNode_Array, ::std::vector<ExprNodeP>());
            } else {
                CLEAR_PARSE_FLAGS_EXPR(lex);
                auto first = Parse_Expr0(lex);
                if (GET_TOK(tok, lex) == TOK_SEMICOLON) {
                    // Repetiion
                    auto count = Parse_Expr0(lex);
                    GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                    return NEWNODE(AST::ExprNode_Array, ::std::move(first), ::std::move(count));
                } else {
                    ::std::vector<ExprNodeP> items;
                    items.push_back(::std::move(first));
                    while (tok.type() == TOK_COMMA) {
                        if (GET_TOK(tok, lex) == TOK_SQUARE_CLOSE) {
                            break;
                        } else {
                            PUTBACK(tok, lex);
                        }
                        items.push_back(Parse_Expr0(lex));
                        GET_TOK(tok, lex);
                    }
                    CHECK_TOK(tok, TOK_SQUARE_CLOSE);
                    return NEWNODE(AST::ExprNode_Array, ::std::move(items));
                }
            }
            throw ParseError::BugCheck(lex, "Array literal fell");
        default:
            throw ParseError::Unexpected(lex, tok);
    }
}

ExprNodeP Parse_ExprVal(TokenStream& lex) {
    auto attrs = Parse_ItemAttrs(lex);
    auto rv = Parse_ExprVal_Inner(lex);
    rv->set_attrs(std::move(attrs));
    return rv;
}

ExprNodeP Parse_ExprMacro(TokenStream& lex, AST::Path path) {
    Token tok;

    RcString ident;
    if (lex.getTokenIf(TOK_IDENT, tok)) {
        ident = tok.ident().name;
    }

    bool is_macro = (path.is_trivial() && path.as_trivial() == "macro_rules");

    bool is_braced = lex.lookahead(0) == TOK_BRACE_OPEN;

    if (is_macro) {
        lex.push_hygine();
    }
    TokenTree tt = Parse_TT(lex, true);
    if (tt.is_token()) {
        throw ParseError::Unexpected(lex, tt.tok());
    }
    if (is_macro) {
        lex.pop_hygine();
    }

    DEBUG("name=" << path << ", ident=" << ident << ", tt=" << tt);
    return NEWNODE(AST::ExprNode_Macro, mv$(path), mv$(ident), mv$(tt), is_braced);
}

// Token Tree Parsing
TokenTree Parse_TT(TokenStream& lex, bool unwrapped) {
    TokenTree rv;
    TRACE_FUNCTION_FR("", rv);

    auto edition = lex.get_edition();
    Token tok = lex.getToken();
    eTokenType closer = TOK_PAREN_CLOSE;
    switch (tok.type()) {
        case TOK_PAREN_OPEN:
            closer = TOK_PAREN_CLOSE;
            break;
        case TOK_SQUARE_OPEN:
            closer = TOK_SQUARE_CLOSE;
            break;
        case TOK_BRACE_OPEN:
            closer = TOK_BRACE_CLOSE;
            break;

        case TOK_EOF:
        case TOK_NULL:
        case TOK_PAREN_CLOSE:
        case TOK_SQUARE_CLOSE:
        case TOK_BRACE_CLOSE:
            throw ParseError::Unexpected(lex, tok);
        default:
            rv = TokenTree(edition, lex.get_hygiene(), mv$(tok));
            DEBUG(rv);
            return rv;
    }

    ::std::vector<TokenTree> items;
    if (!unwrapped) {
        items.push_back(TokenTree(edition, lex.get_hygiene(), mv$(tok)));
    }
    while (!lex.getTokenIf(closer, tok) && !lex.getTokenIf(TOK_EOF, tok)) {
        if (lex.lookahead(0) == TOK_NULL) {
            throw ParseError::Unexpected(lex, lex.getToken());
        }
        items.push_back(Parse_TT(lex, false));
    }
    if (!unwrapped) {
        items.push_back(TokenTree(lex.get_edition(), lex.get_hygiene(), mv$(tok)));
    }
    rv = TokenTree(edition, lex.get_hygiene(), mv$(items));
    DEBUG(rv);
    return rv;
}

#undef NEWNODE
#undef LEFTASSOC

#include "parse_parseerror.h"
#include "parse_common.h"
#include "ast_ast.h"

AST::Path Parse_Path(TokenStream& lex, eParsePathGenericMode generic_mode);
AST::Path Parse_Path(TokenStream& lex, bool is_abs, eParsePathGenericMode generic_mode);
::std::vector<AST::PathNode> Parse_PathNodes(TokenStream& lex, eParsePathGenericMode generic_mode);
AST::PathParams Parse_Path_GenericList(TokenStream& lex);

AST::Path Parse_Path(TokenStream& lex, eParsePathGenericMode generic_mode) {
    TRACE_FUNCTION_F("generic_mode=" << generic_mode);

    Token tok;
    switch (GET_TOK(tok, lex)) {
        case TOK_INTERPOLATED_PATH:
            return mv$(tok.frag_path());

        case TOK_RWORD_SELF:
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            return AST::Path::new_self(Parse_PathNodes(lex, generic_mode));

        case TOK_RWORD_SUPER: {
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            unsigned int count = 1;
            while (LOOK_AHEAD(lex) == TOK_RWORD_SUPER) {
                count += 1;
                GET_TOK(tok, lex);
                if (lex.lookahead(0) != TOK_DOUBLE_COLON) {
                    return AST::Path::new_super(count, {});
                }
                GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            }
            return AST::Path::new_super(count, Parse_PathNodes(lex, generic_mode));
        }

        case TOK_RWORD_CRATE:
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            return Parse_Path(lex, true, generic_mode);
        case TOK_DOUBLE_COLON:
            if (lex.lookahead(0) == TOK_STRING) {
            }
            // QUIRK: `::crate::foo` is valid (semi-surprisingly)
            // TODO: Reference?
            else if (lex.lookahead(0) == TOK_RWORD_CRATE) {
            } else if (lex.edition_after(AST::Edition::Rust2018)) {
                // The first component is a crate name
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                // HACK: if the crate name starts with `=` it's a 2018 absolute path (references a crate loaded with `--extern`)
                auto crate_name = RcString(std::string("=") + tok.ident().name.c_str());
                std::vector<AST::PathNode> nodes;
                if (lex.lookahead(0) == TOK_DOUBLE_COLON) {
                    GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
                    nodes = Parse_PathNodes(lex, generic_mode);
                }
                return AST::Path(crate_name, ::std::move(nodes));
            }
            return Parse_Path(lex, true, generic_mode);

        //case TOK_THINARROW_LEFT:
        //    lex.putback( Token(TOK_DASH) );
        //    if(0)
        case TOK_DOUBLE_LT:
            lex.putback(Token(TOK_LT));
        case TOK_LT: {
            TypeRef ty = Parse_Type(lex, true); // Allow trait objects without parens
            if (GET_TOK(tok, lex) == TOK_RWORD_AS) {
                ::AST::Path trait = Parse_Path(lex, PATH_GENERIC_TYPE);
                GET_CHECK_TOK(tok, lex, TOK_GT);
                GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
                return AST::Path::new_ufcs_trait(mv$(ty), mv$(trait), Parse_PathNodes(lex, generic_mode));
            } else {
                PUTBACK(tok, lex);
                GET_CHECK_TOK(tok, lex, TOK_GT);
                // TODO: Terminating the "path" here is sometimes valid?
                GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
                // NOTE: <Foo>::BAR is actually `<Foo as _>::BAR` (in mrustc parleance)
                //return AST::Path(AST::Path::TagUfcs(), mv$(ty), Parse_PathNodes(lex, generic_mode));
                return AST::Path::new_ufcs_ty(mv$(ty), Parse_PathNodes(lex, generic_mode));
            }
            throw "";
        }

        default:
            PUTBACK(tok, lex);
            return Parse_Path(lex, false, generic_mode);
    }
}

AST::Path Parse_Path(TokenStream& lex, bool is_abs, eParsePathGenericMode generic_mode) {
    Token tok;
    if (is_abs) {
        // QUIRK: `::crate::foo` is valid (semi-surprisingly)
        if (lex.getTokenIf(TOK_RWORD_CRATE)) {
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            return AST::Path("", Parse_PathNodes(lex, generic_mode));
        } else if (lex.getTokenIf(TOK_STRING, tok)) {
            auto cratename = RcString::new_interned(tok.str());
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            return AST::Path(cratename, Parse_PathNodes(lex, generic_mode));
        } else {
            return AST::Path("", Parse_PathNodes(lex, generic_mode));
        }
    } else {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto hygine = tok.ident().hygiene;
        DEBUG("hygine = " << hygine);
        PUTBACK(tok, lex);
        return AST::Path::new_relative(mv$(hygine), Parse_PathNodes(lex, generic_mode));
    }
}

::std::vector<AST::PathNode> Parse_PathNodes(TokenStream& lex, eParsePathGenericMode generic_mode) {
    TRACE_FUNCTION_F("generic_mode=" << generic_mode);

    Token tok;
    ::std::vector<AST::PathNode> ret;

    while (true) {
        ::AST::PathParams params;

        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto component = mv$(tok.ident().name);

        if (generic_mode == PATH_GENERIC_TYPE) {
            // If `foo::<` is seen in type context, then consume the `::` and continue on.
            if (lex.lookahead(0) == TOK_DOUBLE_COLON && (lex.lookahead(1) == TOK_LT || lex.lookahead(1) == TOK_DOUBLE_LT || lex.lookahead(1) == TOK_THINARROW_LEFT)) {
                GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            }
            if (lex.lookahead(0) == TOK_LT || lex.lookahead(0) == TOK_DOUBLE_LT || lex.lookahead(0) == TOK_THINARROW_LEFT) {
                GET_TOK(tok, lex);
                // HACK! Handle breaking << into < <
                if (tok.type() == TOK_DOUBLE_LT) {
                    lex.putback(Token(TOK_LT));
                }
                if (tok.type() == TOK_THINARROW_LEFT) {
                    lex.putback(Token(TOK_DASH));
                }

                // Type-mode generics "::path::to::Type<A,B>"
                params = Parse_Path_GenericList(lex);
            }
            // HACK - 'Fn*(...) -> ...' notation
            else if (lex.lookahead(0) == TOK_PAREN_OPEN) {
                auto ps = lex.start_span();
                DEBUG("Fn() hack");
                ::std::vector<TypeRef> args;
                GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
                do {
                    // Trailing comma or empty list support
                    if (lex.lookahead(0) == TOK_PAREN_CLOSE) {
                        GET_TOK(tok, lex);
                        break;
                    }
                    args.push_back(Parse_Type(lex));
                } while (GET_TOK(tok, lex) == TOK_COMMA);
                CHECK_TOK(tok, TOK_PAREN_CLOSE);

                TypeRef ret_type = TypeRef(TypeRef::TagUnit(), lex.point_span());
                if (lex.lookahead(0) == TOK_THINARROW) {
                    GET_TOK(tok, lex);
                    ret_type = Parse_Type(lex, false);
                }
                DEBUG("- Fn(" << args << ")->" << ret_type << "");

                // Encode into path, by converting Fn(A,B)->C into Fn<(A,B),Ret=C>
                params = ::AST::PathParams();
                params.m_is_paren = true;
                params.m_entries.push_back(TypeRef(TypeRef::TagTuple(), lex.end_span(ps), mv$(args)));
                params.m_entries.push_back(::std::make_pair(AST::PathNode(RcString::new_interned("Output")), mv$(ret_type)));
            } else {
            }
        }
        if (lex.lookahead(0) != TOK_DOUBLE_COLON) {
            ret.push_back(AST::PathNode(component, mv$(params)));
            break;
        }
        GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
        if (generic_mode == PATH_GENERIC_EXPR && (lex.lookahead(0) == TOK_LT || lex.lookahead(0) == TOK_DOUBLE_LT || lex.lookahead(0) == TOK_THINARROW_LEFT)) {
            GET_TOK(tok, lex);
            // HACK! Handle breaking << into < <
            if (tok.type() == TOK_DOUBLE_LT) {
                lex.putback(Token(TOK_LT));
            }
            if (tok.type() == TOK_THINARROW_LEFT) {
                lex.putback(Token(TOK_DASH));
            }

            // Expr-mode generics "::path::to::function::<Type1,Type2>(arg1, arg2)"
            params = Parse_Path_GenericList(lex);
            if (lex.lookahead(0) != TOK_DOUBLE_COLON) {
                ret.push_back(AST::PathNode(component, mv$(params)));
                // Break out of loop down to return
                break;
            }
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
        }
        ret.push_back(AST::PathNode(component, mv$(params)));
    }
    DEBUG("ret = " << ret);
    return ret;
}

/// Parse a list of parameters within a path
::AST::PathParams Parse_Path_GenericList(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    ::AST::PathParams rv;

    do {
        if (LOOK_AHEAD(lex) == TOK_GT || LOOK_AHEAD(lex) == TOK_DOUBLE_GT || LOOK_AHEAD(lex) == TOK_GTE || LOOK_AHEAD(lex) == TOK_DOUBLE_GT_EQUAL) {
            GET_TOK(tok, lex);
            break;
        }
        switch (GET_TOK(tok, lex)) {
            case TOK_LIFETIME:
                rv.m_entries.push_back(AST::LifetimeRef(/*lex.point_span(),*/ tok.ident()));
                break;
            case TOK_RWORD_TRUE:
            case TOK_RWORD_FALSE:
            case TOK_DASH:
            case TOK_INTEGER:
            case TOK_FLOAT:
            case TOK_INTERPOLATED_EXPR:
            case TOK_BRACE_OPEN:
                PUTBACK(tok, lex);
                rv.m_entries.push_back(Parse_Expr13(lex));
                break;
            default:
                PUTBACK(tok, lex);
                rv.m_entries.push_back(Parse_Type(lex));
                if (lex.lookahead(0) == TOK_EQUAL || lex.lookahead(0) == TOK_COLON) {
                    auto sp = lex.point_span();
                    // Uh-oh, the previously-parsed type was actually an ATY name (with generics, probably)
                    // - Decode the above type into the name
                    auto& ty = rv.m_entries.back().as_Type();
                    if (!ty.is_path()) {
                        ERROR(sp, E0000, "Unexpected = or : after non-trivial type path - " << ty);
                    }
                    auto& p = ty.path();
                    if (!p.m_class.is_Relative()) {
                        ERROR(sp, E0000, "Unexpected = or : after non-trivial type path - " << ty);
                    }
                    if (p.m_class.as_Relative().nodes.size() != 1) {
                        ERROR(sp, E0000, "Unexpected = or : after non-trivial type path - " << ty);
                    }
                    auto n = std::move(p.m_class.as_Relative().nodes[0]);
                    rv.m_entries.pop_back();
                    if (lex.getTokenIf(TOK_EQUAL)) {
                        rv.m_entries.push_back(::std::make_pair(mv$(n), Parse_Type(lex, false)));
                    } else if (lex.getTokenIf(TOK_COLON)) {
                        std::vector<AST::Path> traits;
                        // TODO: Trait list instead of duplicating the name
                        for (;;) {
                            traits.push_back(Parse_Path(lex, PATH_GENERIC_TYPE));
                            if (lex.lookahead(0) != TOK_PLUS) {
                                break;
                            }
                            GET_CHECK_TOK(tok, lex, TOK_PLUS);
                            // Allow trailing `+`
                            if (lex.lookahead(0) == TOK_COMMA || lex.lookahead(0) == TOK_PAREN_CLOSE || lex.lookahead(0) == TOK_GT) {
                                break;
                            }
                        }
                        rv.m_entries.push_back(::std::make_pair(mv$(n), std::move(traits)));
                    } else {
                        throw "Unreachable";
                    }
                }
                break;
        }
    } while (GET_TOK(tok, lex) == TOK_COMMA);

    // HACK: Split >> into >
    if (tok.type() == TOK_DOUBLE_GT_EQUAL) {
        lex.putback(Token(TOK_GTE));
    } else if (tok.type() == TOK_GTE) {
        lex.putback(Token(TOK_EQUAL));
    } else if (tok.type() == TOK_DOUBLE_GT) {
        lex.putback(Token(TOK_GT));
    } else {
        CHECK_TOK(tok, TOK_GT);
    }

    return rv;
}

#include "parse_common.h"
#include "parse_parseerror.h"
#include "ast_expr.h" // To convert :expr

// NEWNODE is needed for the Value pattern type
typedef ::std::unique_ptr<AST::ExprNode> PatternExprNodeP;
#define NEWNODE(type, ...) PatternExprNodeP(new type(__VA_ARGS__))
using AST::ExprNode;

AST::Pattern Parse_Pattern1(TokenStream& lex, AllowOrPattern allow_or);
AST::Pattern::Value Parse_PatternValue(TokenStream& lex);
AST::Pattern::TuplePat Parse_PatternTuple(TokenStream& lex, bool* maybe_just_paren = nullptr);
AST::Pattern Parse_PatternReal_Slice(TokenStream& lex);
AST::Pattern Parse_PatternReal_Path(TokenStream& lex, ProtoSpan ps, AST::Path path);
AST::Pattern Parse_PatternStruct(TokenStream& lex, ProtoSpan ps, AST::Path path);

AST::Pattern Parse_PatternReal(TokenStream& lex, AllowOrPattern allow_or);
AST::Pattern Parse_PatternReal1(TokenStream& lex, AllowOrPattern allow_or);

/// Parse a pattern
///
/// Examples:
/// - `Enum::Variant(a)`
/// - `(1, a)`
/// - `1 ... 2`
/// - `"string"`
/// - `mut x`
/// - `mut x @ 1 ... 2`
AST::Pattern Parse_Pattern(TokenStream& lex, AllowOrPattern allow_or) {
    auto ps = lex.start_span();
    if (allow_or == AllowOrPattern::Yes) {
        lex.getTokenIf(TOK_PIPE);
    }
    auto rv = Parse_Pattern1(lex, allow_or);
    if (allow_or == AllowOrPattern::Yes && lex.lookahead(0) == TOK_PIPE) {
        // NOTE: Legal for refutable positions (as long as all possibilities are covered)
        std::vector<AST::Pattern> pats;
        pats.push_back(std::move(rv));
        while (lex.lookahead(0) == TOK_PIPE) {
            lex.getToken();
            pats.push_back(Parse_Pattern1(lex, allow_or));
        }
        return AST::Pattern(lex.end_span(ps), AST::Pattern::Data::make_Or(mv$(pats)));
    } else {
        return rv;
    }
}

AST::Pattern Parse_Pattern1(TokenStream& lex, AllowOrPattern allow_or) {
    TRACE_FUNCTION;
    auto ps = lex.start_span();

    Token tok;
    tok = lex.getToken();

    // TODO: Why is this here explicitly?
    if (tok.type() == TOK_IDENT && lex.lookahead(0) == TOK_EXCLAM) {
        lex.getToken();
        return AST::Pattern(AST::Pattern::TagMacro(), lex.end_span(ps), box$(Parse_MacroInvocation(ps, tok.ident().name, lex)));
    }
    if (tok.type() == TOK_INTERPOLATED_PATTERN) {
        return mv$(tok.frag_pattern());
    }

    bool expect_bind = false;
    auto bind_type = AST::PatternBinding::Type::MOVE;
    bool is_mut = false;
    // 1. Mutablity + Reference
    if (tok.type() == TOK_RWORD_REF) {
        expect_bind = true;
        tok = lex.getToken();
        if (tok.type() == TOK_RWORD_MUT) {
            bind_type = AST::PatternBinding::Type::MUTREF;
            GET_TOK(tok, lex);
        } else {
            bind_type = AST::PatternBinding::Type::REF;
        }
    } else if (tok.type() == TOK_RWORD_MUT) {
        is_mut = true;
        expect_bind = true;
        GET_TOK(tok, lex);
    } else {
        // Fall through
    }

    AST::PatternBinding binding;
    AST::Pattern pat;
    // If a 'ref' or 'mut' annotation was seen, the next name must be a binding name
    if (expect_bind) {
        CHECK_TOK(tok, TOK_IDENT);
        auto bind_name = tok.ident();
        // If there's no '@' after it, it's a name binding only (_ pattern)
        if (GET_TOK(tok, lex) != TOK_AT) {
            PUTBACK(tok, lex);
            return AST::Pattern(AST::Pattern::TagBind(), lex.end_span(ps), mv$(bind_name), bind_type, is_mut);
        }
        binding = AST::PatternBinding(mv$(bind_name), bind_type, is_mut);

        // '@' consumed, move on to next token
        //GET_TOK(tok, lex);
        pat = Parse_Pattern1(lex, allow_or);
    }
    // Otherwise, handle MaybeBind
    else if (tok.type() == TOK_IDENT) {
        switch (LOOK_AHEAD(lex)) {
            // Known path `ident::`
            case TOK_DOUBLE_COLON:
            // Known struct `Ident {` or `Ident (`
            case TOK_BRACE_OPEN:
            case TOK_PAREN_OPEN:
            // Known value `IDENT ...`
            case TOK_DOUBLE_DOT:
            case TOK_TRIPLE_DOT:
            case TOK_DOUBLE_DOT_EQUAL:
                PUTBACK(tok, lex);
                pat = Parse_PatternReal(lex, allow_or);
                break;
            // Known binding `ident @`
            case TOK_AT:
                binding = AST::PatternBinding(tok.ident(), bind_type /*MOVE*/, is_mut /*false*/);
                GET_TOK(tok, lex); // '@'
                pat = Parse_Pattern1(lex, allow_or);
                break;
            default: { // Maybe bind
                auto name = tok.ident();
                // if the pattern can be refuted (i.e this could be an enum variant), return MaybeBind
                if (true /*is_refutable*/) {
                    assert(bind_type == ::AST::PatternBinding::Type::MOVE);
                    assert(is_mut == false);
                    return AST::Pattern(AST::Pattern::TagMaybeBind(), lex.end_span(ps), mv$(name));
                }
                // Otherwise, it IS a binding
                else {
                    return AST::Pattern(AST::Pattern::TagBind(), lex.end_span(ps), mv$(name), bind_type, is_mut);
                }
                throw "";
            }
        }
    } else {
        // Otherwise, fall through
        PUTBACK(tok, lex);
        pat = Parse_PatternReal(lex, allow_or);
    }
    if (binding.is_valid()) {
        pat.bindings().insert(pat.bindings().begin(), mv$(binding));
    }
    return pat;
}

AST::Pattern Parse_PatternReal(TokenStream& lex, AllowOrPattern allow_or) {
    Token tok;
    if (LOOK_AHEAD(lex) == TOK_INTERPOLATED_PATTERN) {
        GET_TOK(tok, lex);
        return mv$(tok.frag_pattern());
    }
    auto ps = lex.start_span();
    AST::Pattern ret = Parse_PatternReal1(lex, allow_or);
    if ((GET_TOK(tok, lex) == TOK_TRIPLE_DOT) || tok.type() == TOK_DOUBLE_DOT_EQUAL) {
        if (!ret.data().is_Value()) {
            throw ParseError::Generic(lex, "Using '...' with a non-value on left");
        }
        auto& ret_v = ret.data().as_Value();
        auto leftval = std::move(ret_v.start);

        auto rightval = Parse_PatternValue(lex);
        if (rightval.is_Invalid()) {
            throw ParseError::Generic(lex, "Using '...' with a no RHS value");
        }

        return AST::Pattern(lex.end_span(ps), AST::Pattern::Data::make_Value({mv$(leftval), mv$(rightval)}));
    } else if (tok.type() == TOK_DOUBLE_DOT) {
        if (!ret.data().is_Value()) {
            throw ParseError::Generic(lex, "Using `..` with a non-value on left");
        }
        auto& ret_v = ret.data().as_Value();
        auto leftval = std::move(ret_v.start);

        auto rightval = Parse_PatternValue(lex);
        if (rightval.is_Invalid()) {
            // Right-open range!
            // - Perfectly valid
        }

        return AST::Pattern(lex.end_span(ps), AST::Pattern::Data::make_ValueLeftInc({mv$(leftval), mv$(rightval)}));
    } else {
        PUTBACK(tok, lex);
        return ret;
    }
}

AST::Pattern::Value Parse_PatternValue(TokenStream& lex) {
    TRACE_FUNCTION;

    Token tok;
    switch (GET_TOK(tok, lex)) {
        case TOK_RWORD_CRATE:
        case TOK_RWORD_SELF:
        case TOK_RWORD_SUPER:
        case TOK_IDENT:
        case TOK_LT:
        case TOK_DOUBLE_LT:
        case TOK_INTERPOLATED_PATH:
        case TOK_DOUBLE_COLON:
            PUTBACK(tok, lex);
            return AST::Pattern::Value::make_Named(Parse_Path(lex, PATH_GENERIC_EXPR));

        case TOK_DASH:
            if (GET_TOK(tok, lex) == TOK_INTEGER) {
                auto dt = tok.datatype();
                // TODO: Ensure that the type is ANY or a signed integer
                return AST::Pattern::Value::make_Integer({dt, ~tok.intval() + 1u});
            } else if (tok.type() == TOK_FLOAT) {
                return AST::Pattern::Value::make_Float({tok.datatype(), -tok.floatval()});
            } else {
                throw ParseError::Unexpected(lex, tok, {TOK_INTEGER, TOK_FLOAT});
            }
        case TOK_FLOAT:
            return AST::Pattern::Value::make_Float({tok.datatype(), tok.floatval()});
        case TOK_INTEGER:
            return AST::Pattern::Value::make_Integer({tok.datatype(), tok.intval()});
        case TOK_RWORD_TRUE:
            return AST::Pattern::Value::make_Integer({CORETYPE_BOOL, U128(1)});
        case TOK_RWORD_FALSE:
            return AST::Pattern::Value::make_Integer({CORETYPE_BOOL, U128(0)});
        case TOK_STRING:
            return AST::Pattern::Value::make_String(mv$(tok.str()));
        case TOK_BYTESTRING:
            return AST::Pattern::Value::make_ByteString({mv$(tok.str())});
        case TOK_INTERPOLATED_EXPR: {
            auto e = tok.take_frag_node();
            // TODO: Visitor?
            if (auto* n = dynamic_cast<AST::ExprNode_String*>(e.get())) {
                return AST::Pattern::Value::make_String(mv$(n->m_value));
            } else if (auto* n = dynamic_cast<AST::ExprNode_ByteString*>(e.get())) {
                return AST::Pattern::Value::make_ByteString({mv$(n->m_value)});
            } else if (auto* n = dynamic_cast<AST::ExprNode_Bool*>(e.get())) {
                return AST::Pattern::Value::make_Integer({CORETYPE_BOOL, U128(n->m_value ? 1 : 0)});
            } else if (auto* n = dynamic_cast<AST::ExprNode_Integer*>(e.get())) {
                return AST::Pattern::Value::make_Integer({n->m_datatype, n->m_value});
            } else if (auto* n = dynamic_cast<AST::ExprNode_Float*>(e.get())) {
                return AST::Pattern::Value::make_Float({n->m_datatype, n->m_value});
            } else {
                TODO(lex.point_span(), "Convert :expr into a pattern value - " << *e);
            }
        } break;
        default:
            PUTBACK(tok, lex);
            return AST::Pattern::Value::make_Invalid({});
    }
}

AST::Pattern Parse_PatternReal1(TokenStream& lex, AllowOrPattern allow_or) {
    TRACE_FUNCTION;
    auto ps = lex.start_span();

    Token tok;
    AST::Path path;

    switch (GET_TOK(tok, lex)) {
        case TOK_UNDERSCORE:
            return AST::Pattern(lex.end_span(ps), AST::Pattern::Data());
        //case TOK_DOUBLE_DOT:
        //    return AST::Pattern( AST::Pattern::TagWildcard() );
        case TOK_RWORD_BOX:
            return AST::Pattern(AST::Pattern::TagBox(), lex.end_span(ps), Parse_Pattern1(lex, allow_or));
        case TOK_DOUBLE_AMP:
            lex.putback(TOK_AMP);
        case TOK_AMP: {
            DEBUG("Ref");
            // NOTE: Falls back into "Pattern" not "PatternReal" to handle MaybeBind again
            bool is_mut = false;
            if (GET_TOK(tok, lex) == TOK_RWORD_MUT) {
                is_mut = true;
            } else {
                PUTBACK(tok, lex);
            }
            return AST::Pattern(AST::Pattern::TagReference(), lex.end_span(ps), is_mut, Parse_Pattern1(lex, allow_or));
        }
        case TOK_RWORD_CRATE:
        case TOK_RWORD_SELF:
        case TOK_RWORD_SUPER:
        case TOK_IDENT:
        case TOK_LT:
        case TOK_DOUBLE_LT:
        case TOK_INTERPOLATED_PATH:
        case TOK_DOUBLE_COLON:
            PUTBACK(tok, lex);
            return Parse_PatternReal_Path(lex, ps, Parse_Path(lex, PATH_GENERIC_EXPR));
        case TOK_DOUBLE_DOT_EQUAL:
        case TOK_TRIPLE_DOT:
            return AST::Pattern(lex.end_span(ps), AST::Pattern::Data::make_Value({{}, Parse_PatternValue(lex)}));
        case TOK_DOUBLE_DOT:
            return AST::Pattern(lex.end_span(ps), AST::Pattern::Data::make_ValueLeftInc({{}, Parse_PatternValue(lex)}));
        case TOK_DASH:
        case TOK_FLOAT:
        case TOK_INTEGER:
        case TOK_RWORD_TRUE:
        case TOK_RWORD_FALSE:
        case TOK_STRING:
        case TOK_BYTESTRING:
        case TOK_INTERPOLATED_EXPR:
            PUTBACK(tok, lex);
            return AST::Pattern(AST::Pattern::TagValue(), lex.end_span(ps), Parse_PatternValue(lex));

        case TOK_PAREN_OPEN: {
            bool just_paren = false;
            auto tpat = Parse_PatternTuple(lex, &just_paren);
            // If it was `(<pat>)` (and not `(<pat>,)`) then unwrap to the first element
            if (just_paren) {
                assert(tpat.start.size() == 1);
                assert(!tpat.has_wildcard);
                assert(tpat.end.size() == 0);
                return std::move(tpat.start.front());
            }
            return AST::Pattern(AST::Pattern::TagTuple(), lex.end_span(ps), std::move(tpat));
        }
        case TOK_SQUARE_OPEN:
            return Parse_PatternReal_Slice(lex);
        default:
            throw ParseError::Unexpected(lex, tok);
    }
    throw "unreachable";
}

AST::Pattern Parse_PatternReal_Path(TokenStream& lex, ProtoSpan ps, AST::Path path) {
    Token tok;

    switch (GET_TOK(tok, lex)) {
        case TOK_PAREN_OPEN:
            return AST::Pattern(AST::Pattern::TagNamedTuple(), lex.end_span(ps), mv$(path), Parse_PatternTuple(lex, nullptr));
        case TOK_BRACE_OPEN:
            return Parse_PatternStruct(lex, ps, mv$(path));
        default:
            PUTBACK(tok, lex);
            return AST::Pattern(AST::Pattern::TagValue(), lex.end_span(ps), AST::Pattern::Value::make_Named(mv$(path)));
    }
}

AST::Pattern Parse_PatternReal_Slice(TokenStream& lex) {
    auto ps = lex.start_span();
    Token tok;

    ::std::vector<::AST::Pattern> leading;
    ::std::vector<::AST::Pattern> trailing;
    ::AST::PatternBinding inner_binding;
    bool is_split = false;

    while (GET_TOK(tok, lex) != TOK_SQUARE_CLOSE) {
        bool has_binding = true;
        ::AST::PatternBinding binding;
        // `ref [mut] foo ..` or `ref [mut] foo @ ..`
        if (tok.type() == TOK_RWORD_REF && ((lex.lookahead(0) == TOK_IDENT && (lex.lookahead(1) == TOK_DOUBLE_DOT || (lex.lookahead(1) == TOK_AT && lex.lookahead(2) == TOK_DOUBLE_DOT))) || (lex.lookahead(0) == TOK_RWORD_MUT && lex.lookahead(1) == TOK_IDENT && (lex.lookahead(2) == TOK_DOUBLE_DOT || (lex.lookahead(2) == TOK_AT && lex.lookahead(3) == TOK_DOUBLE_DOT))))) {
            auto binding_type = ::AST::PatternBinding::Type::REF;
            if (lex.lookahead(0) == TOK_RWORD_MUT) {
                GET_CHECK_TOK(tok, lex, TOK_RWORD_MUT);
                binding_type = ::AST::PatternBinding::Type::MUTREF;
            }
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            binding = ::AST::PatternBinding(tok.ident(), binding_type, false);
        }
        // `foo ..` or `foo @ ..`
        else if (tok.type() == TOK_IDENT && (lex.lookahead(0) == TOK_DOUBLE_DOT || (lex.lookahead(0) == TOK_AT && lex.lookahead(1) == TOK_DOUBLE_DOT))) {
            binding = ::AST::PatternBinding(tok.ident(), ::AST::PatternBinding::Type::MOVE, false);
        }
        // `_ ..` or `_ @ ..`
        else if (tok.type() == TOK_UNDERSCORE && (lex.lookahead(0) == TOK_DOUBLE_DOT || (lex.lookahead(0) == TOK_AT && lex.lookahead(1) == TOK_DOUBLE_DOT))) {
            // No binding, but switching to trailing
        } else if (tok.type() == TOK_DOUBLE_DOT) {
            // No binding, but switching to trailing
            PUTBACK(tok, lex);
        } else {
            has_binding = false;
        }

        if (has_binding) {
            if (is_split) {
                ERROR(lex.end_span(ps), E0000, "Multiple instances of .. in a slice pattern");
            }

            inner_binding = mv$(binding);
            is_split = true;
            if (lex.lookahead(0) == TOK_AT) {
                GET_CHECK_TOK(tok, lex, TOK_AT);
            }
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_DOT);
        } else {
            PUTBACK(tok, lex);
            if (!is_split) {
                leading.push_back(Parse_Pattern(lex));
            } else {
                trailing.push_back(Parse_Pattern(lex));
            }
        }

        if (GET_TOK(tok, lex) != TOK_COMMA) {
            break;
        }
    }
    CHECK_TOK(tok, TOK_SQUARE_CLOSE);

    if (is_split) {
        return ::AST::Pattern(lex.end_span(ps), ::AST::Pattern::Data::make_SplitSlice({mv$(leading), mv$(inner_binding), mv$(trailing)}));
    } else {
        assert(!inner_binding.is_valid());
        assert(trailing.empty());
        return ::AST::Pattern(lex.end_span(ps), ::AST::Pattern::Data::make_Slice({mv$(leading)}));
    }
}

::AST::Pattern::TuplePat Parse_PatternTuple(TokenStream& lex, bool* just_paren) {
    TRACE_FUNCTION;
    auto sp = lex.start_span();
    Token tok;
    if (just_paren) {
        *just_paren = false;
    }

    ::std::vector<AST::Pattern> leading;
    while (LOOK_AHEAD(lex) != TOK_PAREN_CLOSE && LOOK_AHEAD(lex) != TOK_DOUBLE_DOT) {
        leading.push_back(Parse_Pattern(lex));

        if (GET_TOK(tok, lex) != TOK_COMMA) {
            CHECK_TOK(tok, TOK_PAREN_CLOSE);
            // If this was just a parenthesised pattern, then indicate to the caller
            if (just_paren) {
                *just_paren = (leading.size() == 1);
            }
            return AST::Pattern::TuplePat{mv$(leading), false, {}};
        }
    }

    if (LOOK_AHEAD(lex) != TOK_DOUBLE_DOT) {
        GET_TOK(tok, lex);

        CHECK_TOK(tok, TOK_PAREN_CLOSE);
        return AST::Pattern::TuplePat{mv$(leading), false, {}};
    }
    GET_CHECK_TOK(tok, lex, TOK_DOUBLE_DOT);

    ::std::vector<AST::Pattern> trailing;
    if (GET_TOK(tok, lex) == TOK_COMMA) {
        while (LOOK_AHEAD(lex) != TOK_PAREN_CLOSE) {
            trailing.push_back(Parse_Pattern(lex));

            if (GET_TOK(tok, lex) != TOK_COMMA) {
                PUTBACK(tok, lex);
                break;
            }
        }
        GET_TOK(tok, lex);
    }

    CHECK_TOK(tok, TOK_PAREN_CLOSE);
    return ::AST::Pattern::TuplePat{mv$(leading), true, mv$(trailing)};
}

AST::Pattern Parse_PatternStruct(TokenStream& lex, ProtoSpan ps, AST::Path path) {
    TRACE_FUNCTION;
    Token tok;

    // #![feature(relaxed_adts)]
    if (LOOK_AHEAD(lex) == TOK_INTEGER) {
        bool split_allowed = false;
        ::std::map<unsigned int, AST::Pattern> pats;
        while (GET_TOK(tok, lex) == TOK_INTEGER) {
            unsigned int ofs = static_cast<unsigned int>(tok.intval().truncate_u64());
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto val = Parse_Pattern(lex);
            if (!pats.insert(::std::make_pair(ofs, mv$(val))).second) {
                ERROR(lex.point_span(), E0000, "Duplicate index");
            }

            if (GET_TOK(tok, lex) == TOK_BRACE_CLOSE) {
                break;
            }
            CHECK_TOK(tok, TOK_COMMA);
        }
        if (tok.type() == TOK_DOUBLE_DOT) {
            split_allowed = true;
            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_BRACE_CLOSE);

        bool has_split = false;
        ::std::vector<AST::Pattern> leading;
        ::std::vector<AST::Pattern> trailing;
        unsigned int i = 0;
        for (auto& p : pats) {
            if (p.first != i) {
                if (has_split || !split_allowed) {
                    ERROR(lex.point_span(), E0000, "Missing index " << i);
                }
                has_split = true;
                i = p.first;
            }
            if (!has_split) {
                leading.push_back(mv$(p.second));
            } else {
                trailing.push_back(mv$(p.second));
            }
            i++;
        }

        return AST::Pattern(AST::Pattern::TagNamedTuple(), lex.end_span(ps), mv$(path), AST::Pattern::TuplePat{mv$(leading), has_split, mv$(trailing)});
    }

    bool is_exhaustive = true;
    ::std::vector<AST::StructPatternEntry> subpats;
    do {
        if (lex.lookahead(0) == TOK_BRACE_CLOSE) {
            GET_TOK(tok, lex);
            break;
        }
        if (lex.lookahead(0) == TOK_DOUBLE_DOT) {
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_DOT);
            is_exhaustive = false;
            GET_TOK(tok, lex);
            break;
        }

        auto attrs = Parse_ItemAttrs(lex);

        GET_TOK(tok, lex);
        DEBUG("tok = " << tok);

        auto inner_ps = lex.start_span();
        bool is_short_bind = false;
        bool is_box = false;
        auto bind_type = AST::PatternBinding::Type::MOVE;
        bool is_mut = false;
        if (tok.type() == TOK_RWORD_BOX) {
            is_box = true;
            is_short_bind = true;
            GET_TOK(tok, lex);
        }
        if (tok.type() == TOK_RWORD_REF) {
            is_short_bind = true;
            GET_TOK(tok, lex);
            if (tok.type() == TOK_RWORD_MUT) {
                bind_type = AST::PatternBinding::Type::MUTREF;
                GET_TOK(tok, lex);
            } else {
                bind_type = AST::PatternBinding::Type::REF;
            }
        } else if (tok.type() == TOK_RWORD_MUT) {
            is_mut = true;
            is_short_bind = true;
            GET_TOK(tok, lex);
        }

        CHECK_TOK(tok, TOK_IDENT);
        auto field_ident = tok.ident();
        RcString field_name;
        GET_TOK(tok, lex);

        AST::Pattern pat;
        if (is_short_bind || tok.type() != TOK_COLON) {
            PUTBACK(tok, lex);
            pat = AST::Pattern(lex.end_span(inner_ps), {});
            field_name = field_ident.name;
            pat.bindings().push_back(AST::PatternBinding(mv$(field_ident), bind_type, is_mut));
            if (is_box) {
                pat = AST::Pattern(AST::Pattern::TagBox(), lex.end_span(inner_ps), mv$(pat));
            }
        } else {
            CHECK_TOK(tok, TOK_COLON);
            field_name = mv$(field_ident.name);
            pat = Parse_Pattern(lex);
        }

        subpats.push_back(AST::StructPatternEntry{mv$(attrs), mv$(field_name), mv$(pat)});
    } while (GET_TOK(tok, lex) == TOK_COMMA);
    CHECK_TOK(tok, TOK_BRACE_CLOSE);

    return AST::Pattern(AST::Pattern::TagStruct(), lex.end_span(ps), ::std::move(path), ::std::move(subpats), is_exhaustive);
}

#undef NEWNODE

#include "ast_ast.h"
#include "ast_crate.h"
#include "parse_parseerror.h"
#include "parse_common.h"
#include <std/mem/obj_pool.h>
#include <cassert>
#include "hir_hir.h"    // ABI_RUST - TODO: Move elsewhere?
#include "expand_cfg.h" // check_cfg - for `mod nonexistant;`
#include <fstream>        // Used by directory path
#include "parse_lex.h"  // New file lexer
#include "parse_interpolated_fragment.h"
#include "ast_expr.h"
#include "macro_rules_macro_rules.h"
#include "path.h"

template <typename T>
Spanned<T> get_spanned(TokenStream& lex, ::std::function<T()> f) {
    auto ps = lex.start_span();
    auto v = f();
    return Spanned<T>{lex.end_span(ps), mv$(v)};
}

#define GET_SPANNED(type, lex, val) \
    get_spanned<type>(lex, [&]() {  \
        return val;                 \
    })

// Check the next two tokens
#define LOOKAHEAD2(lex, tok1, tok2) ((lex).lookahead(0) == (tok1) && (lex).lookahead(1) == (tok2))

::helpers::path dirname(::std::string input) {
    while (input.size() > 0 && input.back() != '/' && input.back() != '\\') {
        input.pop_back();
    }
    return input;
}

AST::AttributeList Parse_ItemAttrs(TokenStream& lex);
void Parse_ParentAttrs(TokenStream& lex, AST::AttributeList& out);
AST::Attribute Parse_MetaItem(TokenStream& lex);
void Parse_ModRoot(TokenStream& lex, AST::Module& mod, AST::AttributeList& mod_attrs);
bool Parse_MacroInvocation_Opt(TokenStream& lex, AST::MacroInvocation& out_inv);

::AST::Visibility Parse_Publicity(TokenStream& lex, bool allow_restricted /*=true*/) {
    Token tok;
    if (lex.getTokenIf(TOK_INTERPOLATED_VIS, tok)) {
        return tok.take_frag_vis();
    }
    if (lex.lookahead(0) == TOK_RWORD_CRATE && lex.lookahead(1) != TOK_DOUBLE_COLON) {
        GET_CHECK_TOK(tok, lex, TOK_RWORD_CRATE);
        return AST::Visibility::make_restricted(AST::Visibility::Ty::Crate, AST::AbsolutePath("", {}));
    }
    if (lex.getTokenIf(TOK_RWORD_PUB)) {
        if (LOOK_AHEAD(lex) == TOK_PAREN_OPEN) {
            // HACK: tuple structs have a parsing ambiguity around `pub (self::Type,)`
            if (!allow_restricted) {
                if (lex.lookahead(1) == TOK_RWORD_IN)
                    ;
                else if (lex.lookahead(1) == TOK_RWORD_CRATE && lex.lookahead(2) == TOK_PAREN_CLOSE)
                    ;
                else if (lex.lookahead(1) == TOK_RWORD_SUPER && lex.lookahead(2) == TOK_PAREN_CLOSE)
                    ;
                else if (lex.lookahead(1) == TOK_RWORD_SELF && lex.lookahead(2) == TOK_PAREN_CLOSE)
                    ;
                else {
                    return AST::Visibility::make_global();
                }
            }
            auto path = AST::AbsolutePath("", {});
            // Restricted publicity.
            GET_TOK(tok, lex); // '('

            switch (GET_TOK(tok, lex)) {
                case TOK_RWORD_CRATE:
                    // Crate visibility
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return AST::Visibility::make_restricted(AST::Visibility::Ty::PubCrate, std::move(path));
                case TOK_RWORD_SELF:
                    // Private!
                    path = lex.parse_state().get_current_mod().path();
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return AST::Visibility::make_restricted(AST::Visibility::Ty::PubSelf, std::move(path));
                case TOK_RWORD_SUPER:
                    path = lex.parse_state().get_current_mod().path();
                    path.nodes.pop_back();
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return AST::Visibility::make_restricted(AST::Visibility::Ty::PubSuper, std::move(path));
                    break;
                case TOK_RWORD_IN: {
                    AST::Path ast_path;
                    switch (GET_TOK(tok, lex)) {
                        case TOK_DOUBLE_COLON:
                            ast_path = AST::Path("", {});
                            PUTBACK(tok, lex);
                            break;
                        case TOK_IDENT:
                            ast_path = AST::Path::new_relative({}, {});
                            ast_path.nodes().push_back(tok.ident().name);
                            path.nodes.push_back(tok.ident().name);
                            break;
                        case TOK_RWORD_CRATE:
                            ast_path = AST::Path("", {});
                            break;
                        case TOK_RWORD_SELF:
                            ast_path = AST::Path::new_self({});
                            path = lex.parse_state().get_current_mod().path();
                            break;
                        case TOK_RWORD_SUPER:
                            ast_path = AST::Path::new_super(1, {});
                            path = lex.parse_state().get_current_mod().path();
                            path.nodes.pop_back();
                            while (lex.lookahead(0) == TOK_DOUBLE_COLON && lex.lookahead(1) == TOK_RWORD_SUPER) {
                                GET_TOK(tok, lex);
                                GET_TOK(tok, lex);
                                path.nodes.pop_back();
                                ast_path.m_class.as_Super().count += 1;
                            }
                            break;
                        default:
                            throw ParseError::Unexpected(lex, tok);
                    }
                    while (lex.getTokenIf(TOK_DOUBLE_COLON)) {
                        GET_CHECK_TOK(tok, lex, TOK_IDENT);
                        path.nodes.push_back(tok.ident().name);
                        ast_path.nodes().push_back(tok.ident().name);
                    }
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return AST::Visibility::make_restricted(std::move(path), std::move(ast_path));
                }
                default:
                    throw ParseError::Unexpected(lex, tok);
            }
        }
        return AST::Visibility::make_global();
    } else {
        return AST::Visibility::make_restricted(AST::Visibility::Ty::Private, lex.parse_state().get_current_mod().path());
    }
}

::AST::HigherRankedBounds Parse_HRB(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    ::AST::HigherRankedBounds rv;
    GET_CHECK_TOK(tok, lex, TOK_LT);
    do {
        // Support empty lists and comma-terminated lists
        if (lex.lookahead(0) == TOK_GT) {
            GET_TOK(tok, lex);
            break;
        }
        auto attrs = Parse_ItemAttrs(lex);

        switch (GET_TOK(tok, lex)) {
            case TOK_LIFETIME:
                rv.m_lifetimes.push_back(::AST::LifetimeParam(lex.point_span(), ::std::move(attrs), tok.ident()));
                break;
            default:
                throw ParseError::Unexpected(lex, tok, Token(TOK_LIFETIME));
        }
    } while (GET_TOK(tok, lex) == TOK_COMMA);
    CHECK_TOK(tok, TOK_GT);
    return rv;
}

::AST::HigherRankedBounds Parse_HRB_Opt(TokenStream& lex) {
    if (lex.lookahead(0) == TOK_RWORD_FOR) {
        lex.getToken(); // Consume
        return Parse_HRB(lex);
    } else {
        return ::AST::HigherRankedBounds();
    }
}

namespace {
    AST::LifetimeRef get_LifetimeRef(TokenStream& lex, Token tok) {
        CHECK_TOK(tok, TOK_LIFETIME);
        return AST::LifetimeRef(/*lex.point_span(), */ tok.ident());
    }
}

/// Parse type parameters in a definition
void Parse_TypeBound(TokenStream& lex, AST::GenericParams& ret, TypeRef checked_type, AST::HigherRankedBounds outer_hrbs = {}) {
    TRACE_FUNCTION;
    Token tok;

    // Empty bound list
    if (lex.lookahead(0) == TOK_COMMA || lex.lookahead(0) == TOK_BRACE_OPEN || lex.lookahead(0) == TOK_SEMICOLON) {
        return;
    }

    bool is_first = true;
    do {
        auto ps = lex.start_span();
        // If an item terminator is seen (end of item, start of body, list separator), return early.
        if (!is_first && (LOOK_AHEAD(lex) == TOK_SEMICOLON || LOOK_AHEAD(lex) == TOK_COMMA || LOOK_AHEAD(lex) == TOK_GT)) {
            return;
        }
        is_first = false;

        if (lex.getTokenIf(TOK_LIFETIME, tok)) {
            ret.add_bound(AST::GenericBound::make_TypeLifetime({checked_type.clone(), get_LifetimeRef(lex, mv$(tok))}));
        } else if (lex.getTokenIf(TOK_QMARK)) {
            auto hrbs = Parse_HRB_Opt(lex);
            (void)hrbs; // The only valid ?Trait is Sized, which doesn't have any generics
            ret.add_bound(AST::GenericBound::make_MaybeTrait({checked_type.clone(), Parse_Path(lex, PATH_GENERIC_TYPE)}));
        } else {
            if (lex.getTokenIf(TOK_TILDE)) {
                GET_CHECK_TOK(tok, lex, TOK_RWORD_CONST);
            } else if (lex.getTokenIf(TOK_RWORD_CONST)) {
            }
            ::AST::HigherRankedBounds inner_hrls;
            if (lex.getTokenIf(TOK_RWORD_FOR)) {
                inner_hrls = Parse_HRB(lex);
            }
            auto trait_path = Parse_Path(lex, PATH_GENERIC_TYPE);

            auto this_outer_hrbs = (lex.lookahead(0) == TOK_PLUS ? AST::HigherRankedBounds(outer_hrbs) : mv$(outer_hrbs));
            ret.add_bound(AST::GenericBound::make_IsTrait({lex.end_span(ps), mv$(this_outer_hrbs), checked_type.clone(), mv$(inner_hrls), mv$(trait_path)}));
        }
    } while (lex.getTokenIf(TOK_PLUS));
}

/// Parse type parameters within '<' and '>' (definition)
AST::GenericParams Parse_GenericParams(TokenStream& lex) {
    TRACE_FUNCTION;

    AST::GenericParams ret;
    Token tok;
    do {
        if (GET_TOK(tok, lex) == TOK_GT || tok.type() == TOK_GTE) {
            break;
        }

        PUTBACK(tok, lex);
        auto attrs = Parse_ItemAttrs(lex);

        GET_TOK(tok, lex);
        if (tok.type() == TOK_IDENT) {
            auto param_name = tok.ident().name;
            auto param_def = AST::TypeParam(lex.point_span(), ::std::move(attrs), param_name);

            size_t bound_start = SIZE_MAX;
            size_t bound_end = SIZE_MAX;
            auto param_ty = TypeRef(lex.point_span(), param_name);
            if (GET_TOK(tok, lex) == TOK_COLON) {
                bound_start = ret.m_bounds.size();
                Parse_TypeBound(lex, ret, mv$(param_ty));
                bound_end = ret.m_bounds.size();

                GET_TOK(tok, lex);
            }

            if (tok.type() == TOK_EQUAL) {
                param_def.setDefault(Parse_Type(lex));
                GET_TOK(tok, lex);
            }
            ret.add_ty_param(mv$(param_def), bound_start, bound_end);
        } else if (tok.type() == TOK_LIFETIME) {
            size_t bound_start = SIZE_MAX;
            size_t bound_end = SIZE_MAX;
            auto param_name = tok.ident();
            auto ref = get_LifetimeRef(lex, mv$(tok));
            if (GET_TOK(tok, lex) == TOK_COLON) {
                bound_start = ret.m_bounds.size();
                if (lex.lookahead(0) == TOK_LIFETIME) {
                    do {
                        GET_CHECK_TOK(tok, lex, TOK_LIFETIME);
                        ret.add_bound(AST::GenericBound::make_Lifetime({AST::LifetimeRef(ref), get_LifetimeRef(lex, mv$(tok))}));
                    } while (GET_TOK(tok, lex) == TOK_PLUS);
                } else {
                    GET_TOK(tok, lex);
                }
                bound_end = ret.m_bounds.size();
            }
            ret.add_lft_param(::AST::LifetimeParam(lex.point_span(), ::std::move(attrs), param_name), bound_start, bound_end);
        } else if (tok.type() == TOK_RWORD_CONST) {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto param_name = tok.ident();
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto ty = Parse_Type(lex);

            AST::Expr val;
            if (GET_TOK(tok, lex) == TOK_EQUAL) {
                if (lex.lookahead(0) == TOK_BRACE_OPEN) {
                    val = Parse_ExprBlock(lex);
                } else {
                    val = Parse_ExprVal(lex);
                }
                GET_TOK(tok, lex);
            }

            ret.add_value_param(lex.point_span(), mv$(attrs), mv$(param_name), mv$(ty), mv$(val));
        } else {
            throw ParseError::Unexpected(lex, tok, {TOK_IDENT, TOK_LIFETIME});
        }
    } while (tok.type() == TOK_COMMA);

    if (tok.type() == TOK_GT) {
    } else if (tok.type() == TOK_GTE) {
        lex.putback(TOK_EQUAL);
    } else {
        throw ParseError::Unexpected(lex, tok, {TOK_GT, TOK_GTE});
    }
    return ret;
}

AST::GenericParams Parse_GenericParamsOpt(TokenStream& lex) {
    if (lex.getTokenIf(TOK_LT)) {
        return Parse_GenericParams(lex);
    } else {
        return AST::GenericParams();
    }
}

/// Parse the contents of a 'where' clause
void Parse_WhereClause(TokenStream& lex, AST::GenericParams& params) {
    TRACE_FUNCTION;
    Token tok;

    do {
        if (lex.lookahead(0) == TOK_BRACE_OPEN || lex.lookahead(0) == TOK_SEMICOLON) {
            break;
        }

        if (lex.getTokenIf(TOK_LIFETIME, tok)) {
            auto lhs = get_LifetimeRef(lex, std::move(tok));
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            do {
                GET_CHECK_TOK(tok, lex, TOK_LIFETIME);
                auto rhs = get_LifetimeRef(lex, mv$(tok));
                params.add_bound(AST::GenericBound::make_Lifetime({lhs, rhs}));
            } while (lex.getTokenIf(TOK_PLUS));
        }
        // Higher-ranked types/lifetimes
        else if (lex.getTokenIf(TOK_RWORD_FOR)) {
            auto hrbs = Parse_HRB(lex);

            TypeRef type = Parse_Type(lex);
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            Parse_TypeBound(lex, params, mv$(type), mv$(hrbs));
        } else {
            TypeRef type = Parse_Type(lex);
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            Parse_TypeBound(lex, params, mv$(type));
        }
    } while (lex.getTokenIf(TOK_COMMA));
}

// Parse a single function argument
AST::Function::Arg Parse_Function_Arg(TokenStream& lex, bool expect_named) {
    TRACE_FUNCTION_F("expect_named = " << expect_named);
    Token tok;

    auto attrs = Parse_ItemAttrs(lex);

    // If any of the following
    // - Expecting a named parameter (i.e. defining a function in root or impl)
    // - Next token is an underscore (only valid as a pattern here)
    // - Next token is 'mut' (a mutable parameter slot) or 'ref' (ref pattern)
    // - Next two are <ident> ':' (a trivial named parameter)
    // NOTE: When not expecting a named param, destructuring patterns are not allowed
    AST::Pattern pat;
    if (expect_named || LOOK_AHEAD(lex) == TOK_UNDERSCORE || LOOK_AHEAD(lex) == TOK_RWORD_REF || LOOK_AHEAD(lex) == TOK_RWORD_MUT || (LOOK_AHEAD(lex) == TOK_IDENT && lex.lookahead(1) == TOK_COLON)) {
        pat = Parse_Pattern(lex, AllowOrPattern::No);
        GET_CHECK_TOK(tok, lex, TOK_COLON);
    }

    auto ty = Parse_Type(lex);

    return AST::Function::Arg(mv$(pat), mv$(ty), mv$(attrs));
}

/// Parse a function definition (after the 'fn <name>')
AST::Function Parse_FunctionDef(TokenStream& lex, bool allow_self, bool can_be_prototype, std::string abi, AST::Function::Flags flags) {
    TRACE_FUNCTION;
    static const RcString rcstring_self = RcString::new_interned("self");
    static const RcString rcstring_Self = RcString::new_interned("Self");
    ProtoSpan ps = lex.start_span();

    Token tok;

    // Parameters
    AST::GenericParams params = Parse_GenericParamsOpt(lex);

    AST::Function::Arglist args;

    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    GET_TOK(tok, lex);

    // Handle self
    if (tok.type() == TOK_AMP) {
        // By-reference method?

        unsigned int ofs = 0;
        // Handle a lifetime parameter name
        if (lex.lookahead(0) == TOK_LIFETIME) {
            ofs++;
        }

        if (lex.lookahead(ofs) == TOK_RWORD_SELF || (lex.lookahead(ofs) == TOK_RWORD_MUT && lex.lookahead(ofs + 1) == TOK_RWORD_SELF)) {
            auto ps = lex.start_span();
            AST::LifetimeRef lifetime;
            if (GET_TOK(tok, lex) == TOK_LIFETIME) {
                lifetime = get_LifetimeRef(lex, mv$(tok));
                GET_TOK(tok, lex);
            }

            bool is_mut = false;
            if (tok.type() == TOK_RWORD_MUT) {
                is_mut = true;
                GET_TOK(tok, lex);
            }
            CHECK_TOK(tok, TOK_RWORD_SELF);
            auto sp = lex.end_span(ps);
            args.push_back(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), sp, rcstring_self), TypeRef(TypeRef::TagReference(), sp, ::std::move(lifetime), is_mut, TypeRef(sp, rcstring_Self, 0xFFFF))));
            //if( allow_self == false )
            //    ERROR(lex.point_span(), E0000, "Self binding not expected here");

            // Prime tok for next step
            GET_TOK(tok, lex);
        } else {
            // Unbound method
        }
    } else if (tok.type() == TOK_RWORD_MUT) {
        if (LOOK_AHEAD(lex) == TOK_RWORD_SELF) {
            GET_TOK(tok, lex);
            //if( allow_self == false )
            //    throw ParseError::Generic(lex, "Self binding not expected");
            auto binding_sp = lex.end_span(ps);
            TypeRef ty = TypeRef(lex.point_span(), rcstring_Self, 0xFFFF);
            if (GET_TOK(tok, lex) == TOK_COLON) {
                // Typed mut self
                ty = Parse_Type(lex);
            } else {
                PUTBACK(tok, lex);
            }
            args.push_back(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), binding_sp, rcstring_self), mv$(ty)));
            GET_TOK(tok, lex);
        }
    } else if (tok.type() == TOK_RWORD_SELF) {
        // By-value method
        //if( allow_self == false )
        //    throw ParseError::Generic(lex, "Self binding not expected");
        auto binding_sp = lex.end_span(ps);
        TypeRef ty = TypeRef(lex.point_span(), rcstring_Self, 0xFFFF);
        if (GET_TOK(tok, lex) == TOK_COLON) {
            // Typed mut self
            ty = Parse_Type(lex);
        } else {
            PUTBACK(tok, lex);
        }
        args.push_back(AST::Function::Arg(AST::Pattern(AST::Pattern::TagBind(), binding_sp, rcstring_self), mv$(ty)));
        GET_TOK(tok, lex);
    } else {
        // Unbound method
    }

    // In 2018, patterns must always be provided
    if (lex.edition_after(AST::Edition::Rust2018)) {
        can_be_prototype = false;
    }

    bool is_variadic = false;
    if (tok.type() != TOK_PAREN_CLOSE) {
        // Comma after self
        if (args.size()) {
            CHECK_TOK(tok, TOK_COMMA);
        } else {
            PUTBACK(tok, lex);
        }

        // Argument list
        do {
            if (LOOK_AHEAD(lex) == TOK_PAREN_CLOSE) {
                GET_TOK(tok, lex);
                break;
            }
            if (LOOK_AHEAD(lex) == TOK_TRIPLE_DOT) {
                GET_TOK(tok, lex);
                is_variadic = true;
                GET_TOK(tok, lex);
                break;
            }
            args.push_back(Parse_Function_Arg(lex, !can_be_prototype));
        } while (GET_TOK(tok, lex) == TOK_COMMA);
        CHECK_TOK(tok, TOK_PAREN_CLOSE);
    } else {
        // Eat 'tok', negative comparison
    }

    // Return type
    TypeRef ret_type = lex.getTokenIf(TOK_THINARROW) ? Parse_Type(lex) : TypeRef(TypeRef::TagUnit(), lex.point_span());

    // Bounds
    if (lex.getTokenIf(TOK_RWORD_WHERE)) {
        Parse_WhereClause(lex, params);
    }

    return AST::Function(lex.end_span(ps), mv$(abi), mv$(flags), mv$(params), mv$(ret_type), mv$(args), is_variadic);
}

AST::Function Parse_FunctionDefWithCode(TokenStream& lex, bool allow_self, std::string abi, AST::Function::Flags flags) {
    Token tok;
    auto ret = Parse_FunctionDef(lex, allow_self, /*can_be_prototype=*/false, std::move(abi), flags);
    GET_TOK(tok, lex);
    if (tok == TOK_BRACE_OPEN) {
    } else if (tok.type() == TOK_INTERPOLATED_BLOCK) {
    } else if (tok.type() == TOK_SEMICOLON) {
        // Used for #[rustc_intrinsic] tagged functions
        return ret;
    } else {
        throw ParseError::Unexpected(lex, tok, {TOK_BRACE_OPEN, TOK_INTERPOLATED_BLOCK});
    }
    // Enter a new hygine scope for the function (TODO: Should this be in Parse_ExprBlock?)
    lex.push_hygine();
    PUTBACK(tok, lex);
    ret.set_code(Parse_ExprBlock(lex));
    lex.pop_hygine();
    return ret;
}

AST::TypeAlias Parse_TypeAlias(TokenStream& lex) {
    TRACE_FUNCTION;

    Token tok;

    // Params
    AST::GenericParams params = Parse_GenericParamsOpt(lex);

    GET_TOK(tok, lex);
    if (tok.type() == TOK_RWORD_WHERE) {
        Parse_WhereClause(lex, params);
        GET_TOK(tok, lex);
    }
    CHECK_TOK(tok, TOK_EQUAL);

    // Type
    TypeRef type = Parse_Type(lex);
    GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

    return AST::TypeAlias(::std::move(params), ::std::move(type));
}

AST::Struct Parse_Struct(TokenStream& lex, const AST::AttributeList& meta_items) {
    TRACE_FUNCTION;

    Token tok;

    tok = lex.getToken();
    AST::GenericParams params;
    if (tok.type() == TOK_LT) {
        params = Parse_GenericParams(lex);
        tok = lex.getToken();
    }

    if (tok.type() == TOK_PAREN_OPEN) {
        // Tuple structs
        ::std::vector<AST::TupleItem> refs;
        while (!lex.getTokenIf(TOK_PAREN_CLOSE)) {
            auto item_attrs = Parse_ItemAttrs(lex);
            SET_ATTRS(lex, item_attrs);

            auto vis = Parse_Publicity(lex, /*allow_restricted=*/false); // HACK: Disable `pub(restricted)` syntax in tuple structs, due to ambiguity

            refs.push_back(AST::TupleItem(mv$(item_attrs), vis, Parse_Type(lex)));
            if (GET_TOK(tok, lex) != TOK_COMMA) {
                CHECK_TOK(tok, TOK_PAREN_CLOSE);
                break;
            }
        }

        if (lex.getTokenIf(TOK_RWORD_WHERE)) {
            Parse_WhereClause(lex, params);
        }
        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
        //if( refs.size() == 0 )
        //    WARNING( , W000, "Use 'struct Name;' instead of 'struct Name();' ... ning-nong");
        return AST::Struct(mv$(params), mv$(refs));
    } else {
        // Unit-like struct
        if (tok.type() == TOK_RWORD_WHERE) {
            Parse_WhereClause(lex, params);
            tok = lex.getToken();
        }

        if (tok.type() == TOK_SEMICOLON) {
            CHECK_TOK(tok, TOK_SEMICOLON);
            return AST::Struct(mv$(params));
        } else if (tok.type() == TOK_BRACE_OPEN) {
            ::std::vector<AST::StructItem> items;
            while (!lex.getTokenIf(TOK_BRACE_CLOSE)) {
                auto item_attrs = Parse_ItemAttrs(lex);
                SET_ATTRS(lex, item_attrs);

                auto vis = Parse_Publicity(lex);

                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                auto name = tok.ident().name;
                GET_CHECK_TOK(tok, lex, TOK_COLON);
                TypeRef type = Parse_Type(lex);
                AST::Expr default_value = lex.getTokenIf(TOK_EQUAL) ? Parse_Expr(lex) : AST::Expr();

                items.push_back(AST::StructItem(mv$(item_attrs), vis, mv$(name), mv$(type), std::move(default_value)));
                if (GET_TOK(tok, lex) == TOK_BRACE_CLOSE) {
                    break;
                }
                CHECK_TOK(tok, TOK_COMMA);
            }
            //if( items.size() == 0 )
            //    WARNING( , W000, "Use 'struct Name;' instead of 'struct Nam { };' ... ning-nong");
            return AST::Struct(mv$(params), mv$(items));
        } else {
            throw ParseError::Unexpected(lex, tok);
        }
    }
}

AST::Named<AST::Item> Parse_Trait_Item(TokenStream& lex) {
    Token tok;

    auto item_attrs = Parse_ItemAttrs(lex);
    SET_ATTRS(lex, item_attrs);

    auto ps = lex.start_span();

    {
        ::AST::MacroInvocation inv;
        if (Parse_MacroInvocation_Opt(lex, inv)) {
            return ::AST::Named<::AST::Item>{lex.end_span(ps), mv$(item_attrs), AST::Visibility::make_global(), "", ::AST::Item(mv$(inv))};
        }
    }

    // An already-parsed `$item:item` fragment. A trait item *is* an `AST::Named<AST::Item>`, so it is handed straight back.
    if (lex.lookahead(0) == TOK_INTERPOLATED_ITEM) {
        tok = lex.getToken();
        auto item = tok.take_frag_item();
        for (auto& a : item_attrs.m_items) {
            item.attrs.m_items.push_back(std::move(a));
        }
        // Only the kinds a trait body can hold; anything else is a loud TODO rather than silently accepted.
        TU_MATCH_HDRA((item.data), {)
        default:
            TODO(lex.point_span(), "Interpolated item into trait: " << item.data.tag_str());
            TU_ARMA(Function, e) {
                (void)e;
            }
            TU_ARMA(Static, e) {
                (void)e;
            }
            TU_ARMA(Type, e) {
                (void)e;
            }
        }
        return item;
    }

    GET_TOK(tok, lex);
    bool is_specialisable = false;
    if (tok.type() == TOK_IDENT && tok.ident().name == "default") {
        is_specialisable = true;
        GET_TOK(tok, lex);
    }
    // TODO: Mark specialisation
    (void)is_specialisable;

    std::string abi = ABI_RUST;
    AST::Function::Flags fn_flags;

    if (tok.type() == TOK_RWORD_UNSAFE) {
        fn_flags.is_unsafe = true;
        GET_TOK(tok, lex);
    }
    if (tok.type() == TOK_RWORD_ASYNC) {
        fn_flags.is_async = true;
        GET_TOK(tok, lex);
    }
    if (tok.type() == TOK_RWORD_EXTERN) {
        if (GET_TOK(tok, lex) == TOK_STRING) {
            abi = tok.str();
            GET_TOK(tok, lex);
        } else {
            abi = "C";
        }
    }

    RcString name;
    ::AST::Item rv;
    switch (tok.type()) {
        case TOK_RWORD_STATIC: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            name = tok.ident().name;
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto ty = Parse_Type(lex);
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

            ::AST::Expr val;
            if (GET_TOK(tok, lex) == TOK_EQUAL) {
                val = Parse_Expr(lex);
                GET_TOK(tok, lex);
            }
            CHECK_TOK(tok, TOK_SEMICOLON);

            rv = ::AST::Static(::AST::Static::STATIC, mv$(ty), val);
            break;
        }
        case TOK_RWORD_CONST: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            name = tok.ident().name;
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto ty = Parse_Type(lex);

            ::AST::Expr val;
            if (GET_TOK(tok, lex) == TOK_EQUAL) {
                val = Parse_Expr(lex);
                GET_TOK(tok, lex);
            }
            CHECK_TOK(tok, TOK_SEMICOLON);

            rv = ::AST::Static(AST::Static::CONST, mv$(ty), val);
            break;
        }
        // Associated type
        case TOK_RWORD_TYPE: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            name = tok.ident().name;
            auto type_params = Parse_GenericParamsOpt(lex);
            AST::GenericParams bounds;
            if (GET_TOK(tok, lex) == TOK_COLON) {
                // Bounded associated type
                Parse_TypeBound(lex, bounds, TypeRef(lex.point_span(), RcString::new_interned("Self"), 0xFFFF));
                GET_TOK(tok, lex);
            }

            TypeRef default_type = TypeRef(lex.point_span());
            if (tok.type() == TOK_EQUAL) {
                default_type = Parse_Type(lex);
                GET_TOK(tok, lex);
            }
            if (tok.type() == TOK_RWORD_WHERE) {
                Parse_WhereClause(lex, type_params);
                GET_TOK(tok, lex);
            }

            CHECK_TOK(tok, TOK_SEMICOLON);
            rv = ::AST::TypeAlias::new_associated_type(mv$(type_params), mv$(bounds), mv$(default_type));
            break;
        }

        // Functions (possibly unsafe, async, or extern)
        case TOK_RWORD_FN: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            name = tok.ident().name;
            // Self allowed, prototype-form allowed (optional names and no code)
            auto fcn = Parse_FunctionDef(lex, /*allow_self*/ true, /*can_be_proto*/ true, std::move(abi), fn_flags);
            if (lex.lookahead(0) == TOK_BRACE_OPEN) {
                // Enter a new hygine scope for the function body. (TODO: Should this be in Parse_ExprBlock?)
                lex.push_hygine();
                fcn.set_code(Parse_ExprBlock(lex));
                lex.pop_hygine();
            } else if (lex.getTokenIf(TOK_SEMICOLON)) {
                // Accept it
            } else {
                throw ParseError::Unexpected(lex, tok);
            }
            rv = ::std::move(fcn);
            break;
        }
        default:
            throw ParseError::Unexpected(lex, tok);
    }

    return ::AST::Named<::AST::Item>(lex.end_span(ps), mv$(item_attrs), AST::Visibility::make_global(), mv$(name), mv$(rv));
}

AST::Trait Parse_TraitDef(TokenStream& lex, const AST::AttributeList& meta_items, AST::GenericParams params) {
    TRACE_FUNCTION;

    Token tok;

    GET_TOK(tok, lex);

    // Trait bounds "trait Trait : 'lifetime + OtherTrait + OtherTrait2"
    ::std::vector<Spanned<Type_TraitPath>> supertraits;
    ::std::vector<Spanned<AST::LifetimeRef>> lifetimes;
    if (tok.type() == TOK_COLON) {
        // TODO: Just add these as `where Self: <foo>` (would that break typecheck?)
        do {
            if (GET_TOK(tok, lex) == TOK_LIFETIME) {
                lifetimes.push_back(GET_SPANNED(AST::LifetimeRef, lex, ::AST::LifetimeRef(tok.ident())));
            } else if (tok.type() == TOK_BRACE_OPEN) {
                break;
            } else {
                if (tok.type() == TOK_TILDE) {
                    GET_CHECK_TOK(tok, lex, TOK_RWORD_CONST);
                    GET_TOK(tok, lex);
                } else if (tok.type() == TOK_RWORD_CONST) {
                    GET_TOK(tok, lex);
                }
                PUTBACK(tok, lex);
                auto hrbs = Parse_HRB_Opt(lex);
                supertraits.push_back(GET_SPANNED(Type_TraitPath, lex, (Type_TraitPath(mv$(hrbs), Parse_Path(lex, PATH_GENERIC_TYPE)))));
            }
        } while (GET_TOK(tok, lex) == TOK_PLUS);
    }

    if (tok.type() == TOK_RWORD_WHERE) {
        //if( params.ty_params().size() == 0 )
        //    throw ParseError::Generic("Where clause with no generic params");
        Parse_WhereClause(lex, params);
        tok = lex.getToken();
    }

    AST::Trait trait(mv$(params), mv$(supertraits), mv$(lifetimes));

    CHECK_TOK(tok, TOK_BRACE_OPEN);
    while (GET_TOK(tok, lex) != TOK_BRACE_CLOSE) {
        PUTBACK(tok, lex);

        trait.items().push_back(Parse_Trait_Item(lex));
    }

    return trait;
}

AST::Enum Parse_EnumDef(TokenStream& lex, const AST::AttributeList& meta_items) {
    TRACE_FUNCTION;

    Token tok;

    tok = lex.getToken();
    // Type params supporting "where"
    AST::GenericParams params;
    if (tok.type() == TOK_LT) {
        params = Parse_GenericParams(lex);
        if (GET_TOK(tok, lex) == TOK_RWORD_WHERE) {
            Parse_WhereClause(lex, params);
            tok = lex.getToken();
        }
    }

    // Body
    CHECK_TOK(tok, TOK_BRACE_OPEN);
    ::std::vector<AST::EnumVariant> variants;
    while (lex.lookahead(0) != TOK_BRACE_CLOSE) {
        auto sp = lex.start_span();

        auto item_attrs = Parse_ItemAttrs(lex);
        SET_ATTRS(lex, item_attrs);

        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto name = tok.ident().name;
        // Tuple-like variants
        if (lex.getTokenIf(TOK_PAREN_OPEN)) {
            ::std::vector<AST::TupleItem> items;
            // Get type list
            do {
                if (lex.getTokenIf(TOK_PAREN_CLOSE, tok)) {
                    break;
                }

                auto field_attrs = Parse_ItemAttrs(lex);
                auto ty = Parse_Type(lex);
                items.emplace_back(std::move(field_attrs), AST::Visibility::make_global(), std::move(ty));
            } while (GET_TOK(tok, lex) == TOK_COMMA);
            CHECK_TOK(tok, TOK_PAREN_CLOSE);
            variants.push_back(AST::EnumVariant(mv$(item_attrs), mv$(name), mv$(items)));
        }
        // Struct-like variants
        else if (lex.getTokenIf(TOK_BRACE_OPEN)) {
            ::std::vector<::AST::StructItem> fields;
            do {
                if (lex.getTokenIf(TOK_BRACE_CLOSE, tok)) {
                    break;
                }

                auto field_attrs = Parse_ItemAttrs(lex);

                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                auto name = tok.ident().name;
                GET_CHECK_TOK(tok, lex, TOK_COLON);
                auto ty = Parse_Type(lex);
                auto def = lex.getTokenIf(TOK_EQUAL) ? Parse_Expr(lex) : AST::Expr();
                fields.push_back(::AST::StructItem(mv$(field_attrs), AST::Visibility::make_global(), mv$(name), mv$(ty), mv$(def)));
            } while (GET_TOK(tok, lex) == TOK_COMMA);
            CHECK_TOK(tok, TOK_BRACE_CLOSE);

            variants.push_back(AST::EnumVariant(mv$(item_attrs), mv$(name), mv$(fields)));
        }
        // Unit variants
        else {
            variants.push_back(AST::EnumVariant(mv$(item_attrs), mv$(name)));
        }

        if (lex.getTokenIf(TOK_EQUAL)) {
            variants.back().m_discriminant_value = Parse_Expr(lex);
        }

        if (!lex.getTokenIf(TOK_COMMA)) {
            break;
        }
        // Consumed the comma
    }
    GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);

    return AST::Enum(mv$(params), mv$(variants));
}

::AST::Union Parse_Union(TokenStream& lex, AST::AttributeList& meta_items) {
    Token tok;

    TRACE_FUNCTION;

    AST::GenericParams params;
    if (GET_TOK(tok, lex) == TOK_LT) {
        params = Parse_GenericParams(lex);
        if (GET_TOK(tok, lex) == TOK_RWORD_WHERE) {
            Parse_WhereClause(lex, params);
            tok = lex.getToken();
        }
    }

    ::std::vector<::AST::StructItem> variants;

    CHECK_TOK(tok, TOK_BRACE_OPEN);
    do {
        if (LOOK_AHEAD(lex) == TOK_BRACE_CLOSE) {
            GET_TOK(tok, lex);
            break;
        }

        auto item_attrs = Parse_ItemAttrs(lex);
        SET_ATTRS(lex, item_attrs);

        auto vis = Parse_Publicity(lex);

        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto name = tok.ident().name;
        GET_CHECK_TOK(tok, lex, TOK_COLON);

        auto ty = Parse_Type(lex);

        variants.push_back(::AST::StructItem(mv$(item_attrs), mv$(vis), mv$(name), mv$(ty), {}));

    } while (GET_TOK(tok, lex) == TOK_COMMA);
    CHECK_TOK(tok, TOK_BRACE_CLOSE);

    return ::AST::Union(mv$(params), mv$(variants));
}

AST::AttributeList Parse_ItemAttrs(TokenStream& lex) {
    AST::AttributeList rv;
    Token tok;
    while (lex.lookahead(0) == TOK_HASH) {
        GET_CHECK_TOK(tok, lex, TOK_HASH);
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_OPEN);
        rv.push_back(Parse_MetaItem(lex));
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
    }
    return rv;
}

void Parse_ParentAttrs(TokenStream& lex, AST::AttributeList& out) {
    Token tok;
    while (lex.lookahead(0) == TOK_HASH && lex.lookahead(1) == TOK_EXCLAM) {
        GET_CHECK_TOK(tok, lex, TOK_HASH);
        GET_CHECK_TOK(tok, lex, TOK_EXCLAM);
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_OPEN);
        out.push_back(Parse_MetaItem(lex));
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
    }
}

namespace {
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

/// Parse a meta-item declaration (either #![ or #[)
AST::Attribute Parse_MetaItem(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    if (lex.lookahead(0) == TOK_INTERPOLATED_META) {
        GET_TOK(tok, lex);
        return mv$(tok.frag_meta());
    }

    auto ps = lex.start_span();

    AST::AttributeName name;
    // NOTE: After 1.19 mode, values can be present with no name
    if (lex.lookahead(0) != TOK_IDENT && lex.lookahead(0) != TOK_DOUBLE_COLON && !Token::type_is_rword(lex.lookahead(0))) {
        // Put a fake equals token in the queue
        tok = Token(TOK_EQUAL);
    } else {
        name.has_leading = lex.getTokenIf(TOK_DOUBLE_COLON);
        do {
            name.elems.push_back(get_tok_ident_rword(lex));
        } while (GET_TOK(tok, lex) == TOK_DOUBLE_COLON);
    }
    DEBUG("name = " << name);
    TokenTree attr_data;
    switch (tok.type()) {
        case TOK_EQUAL: {
            std::vector<TokenTree> tt;
            tt.push_back(std::move(tok));
            // - Square close (top-level) AND paren close (cfg_attr)
            while (lex.lookahead(0) != TOK_EOF && lex.lookahead(0) != TOK_SQUARE_CLOSE && lex.lookahead(0) != TOK_PAREN_CLOSE && lex.lookahead(0) != TOK_BRACE_CLOSE && lex.lookahead(0) != TOK_COMMA && lex.lookahead(0) != TOK_SEMICOLON) {
                tt.push_back(Parse_TT(lex, false));
            }
            attr_data = TokenTree(lex.get_edition(), lex.get_hygiene(), std::move(tt));
        } break;
        case TOK_PAREN_OPEN:
        case TOK_SQUARE_OPEN: // 1.74 - openssl v0.10.57
            PUTBACK(tok, lex);
            attr_data = Parse_TT(lex, false);
            break;
        default:
            // Empty
            PUTBACK(tok, lex);
            break;
    }
    return AST::Attribute(lex.end_span(ps), name, mv$(attr_data));
}

::AST::Item Parse_Impl(TokenStream& lex, AST::AttributeList& attrs, bool is_unsafe = false) {
    TRACE_FUNCTION;
    Token tok;
    auto ps = lex.start_span();

    AST::GenericParams params;
    // 1. (optional) type parameters
    if (lex.getTokenIf(TOK_LT)) {
        params = Parse_GenericParams(lex);
    }
    // 2. Either a trait name (with type params), or the type to impl

    Spanned<AST::Path> trait_path;

    if (lex.getTokenIf(TOK_RWORD_CONST)) {
        // TODO: Save the const flag?
    }

    // - Handle negative impls specially, which must be a trait
    // "impl !Trait for Type {}"
    // NOTE: Special case to handle `impl ! {}` (used for docs in 1.90)
    if (GET_TOK(tok, lex) == TOK_EXCLAM && lex.lookahead(0) != TOK_BRACE_OPEN) {
        trait_path = GET_SPANNED(::AST::Path, lex, Parse_Path(lex, PATH_GENERIC_TYPE));
        GET_CHECK_TOK(tok, lex, TOK_RWORD_FOR);
        auto impl_type = Parse_Type(lex, true);

        if (GET_TOK(tok, lex) == TOK_RWORD_WHERE) {
            Parse_WhereClause(lex, params);
            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_BRACE_OPEN);
        // negative impls can't have any content
        GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);

        return ::AST::Item::make_NegImpl(AST::ImplDef(mv$(params), mv$(trait_path), mv$(impl_type)));
    }

    // - Don't care which at this stage
    PUTBACK(tok, lex);

    auto impl_type = Parse_Type(lex, true);

    if (GET_TOK(tok, lex) == TOK_RWORD_FOR) {
        // Trickery! All traits parse as valid types, so this works.
        if (!impl_type.is_path()) {
            throw ParseError::Generic(lex, "Trait was not a path");
        }
        trait_path = Spanned<AST::Path>{impl_type.span(), mv$(impl_type.path())};
        // Implementing a trait for another type, get the target type
        if (GET_TOK(tok, lex) == TOK_DOUBLE_DOT) {
            // Default impl
            impl_type = TypeRef(TypeRef::TagInvalid(), lex.point_span());
        } else {
            PUTBACK(tok, lex);
            impl_type = Parse_Type(lex, true);
        }
    } else {
        PUTBACK(tok, lex);
    }

    // Where clause
    if (GET_TOK(tok, lex) == TOK_RWORD_WHERE) {
        Parse_WhereClause(lex, params);
    } else {
        PUTBACK(tok, lex);
    }
    GET_CHECK_TOK(tok, lex, TOK_BRACE_OPEN);

    Parse_ParentAttrs(lex, attrs);

    auto impl = AST::Impl(AST::ImplDef(mv$(params), mv$(trait_path), mv$(impl_type)));

    // A sequence of method implementations
    while (lex.lookahead(0) != TOK_BRACE_CLOSE) {
        Parse_Impl_Item(lex, impl);
    }
    GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);

    return ::AST::Item::make_Impl(mv$(impl));
}

void Parse_Impl_Item(TokenStream& lex, AST::Impl& impl) {
    TRACE_FUNCTION;
    Token tok;

    auto item_attrs = Parse_ItemAttrs(lex);
    SET_ATTRS(lex, item_attrs);

    {
        ::AST::MacroInvocation inv;
        if (Parse_MacroInvocation_Opt(lex, inv)) {
            impl.add_macro_invocation(mv$(inv));
            impl.items().back().attrs = mv$(item_attrs);
            return;
        }
    }
    {
        if (lex.lookahead(0) == TOK_INTERPOLATED_ITEM) {
            tok = lex.getToken();
            auto item = tok.take_frag_item();
            // Attributes are parsed before the fragment is seen, so without this transfer they are dropped - turning a `#[cfg]` that should remove the item into a no-op.
            for (auto& a : item_attrs.m_items) {
                item.attrs.m_items.push_back(std::move(a));
            }
            TU_MATCH_HDRA((item.data), {)
            default:
                TODO(lex.point_span(), "Interpolated item into impl: " << item.data.tag_str());
                TU_ARMA(Function, e) {
                    impl.add_function(item.span, std::move(item.attrs), item.vis, false, item.name, std::move(e));
                }
                // An associated `const` - the only kind of `Static` an impl block can hold, stored as the non-interpolated path stores one.
                TU_ARMA(Static, e) {
                    impl.add_static(item.span, std::move(item.attrs), item.vis, false, item.name, std::move(e));
                }
                //TU_ARMA(Type, e) {
                //    impl.add_type(item.span, std::move(item.attrs), item.vis, false, item.name, std::move(e.m_params), std::move(e.m_type));
                //    }
            }
            return ;
        }
    }

    auto ps = lex.start_span();

    auto vis = Parse_Publicity(lex);
    GET_TOK(tok, lex);

    bool is_specialisable = false;
    if (tok.type() == TOK_IDENT && tok.ident().name == "default") {
        is_specialisable = true;
        GET_TOK(tok, lex);
    }

    ::std::string abi = ABI_RUST;
    AST::Function::Flags fn_flags;
    if (tok.type() == TOK_RWORD_TYPE) {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto name = tok.ident().name;
        auto atype_params = Parse_GenericParamsOpt(lex);
        GET_CHECK_TOK(tok, lex, TOK_EQUAL);
        auto ty = Parse_Type(lex);
        if (lex.getTokenIf(TOK_RWORD_WHERE)) {
            Parse_WhereClause(lex, atype_params);
        }
        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
        impl.add_type(lex.end_span(ps), mv$(item_attrs), vis, is_specialisable, name, mv$(atype_params), mv$(ty));
        return;
    }

    if (tok.type() == TOK_RWORD_UNSAFE) {
        fn_flags.is_unsafe = true;
        GET_TOK(tok, lex);
    }
    if (tok.type() == TOK_RWORD_CONST) {
        GET_TOK(tok, lex);
        if (tok.type() != TOK_RWORD_FN && tok.type() != TOK_RWORD_UNSAFE && !fn_flags.is_unsafe) {
            CHECK_TOK(tok, TOK_IDENT);
            auto name = tok.ident().name;
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto ty = Parse_Type(lex);
            GET_CHECK_TOK(tok, lex, TOK_EQUAL);
            auto val = Parse_Expr(lex);
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

            auto i = ::AST::Static(AST::Static::CONST, mv$(ty), mv$(val));
            impl.add_static(lex.end_span(ps), mv$(item_attrs), vis, is_specialisable, mv$(name), mv$(i));
            return;
        }
        if (tok.type() == TOK_RWORD_UNSAFE) {
            fn_flags.is_unsafe = true;
            GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
        }
        fn_flags.is_const = true;
    }
    if (tok.type() == TOK_RWORD_EXTERN) {
        if (GET_TOK(tok, lex) == TOK_STRING) {
            abi = tok.str();
            GET_TOK(tok, lex);
        } else {
            abi = "C";
        }
    }
    if (tok.type() == TOK_RWORD_ASYNC) {
        fn_flags.is_async = true;
        if (lex.getTokenIf(TOK_RWORD_UNSAFE)) {
            fn_flags.is_unsafe = true;
        }
        GET_TOK(tok, lex);
    }
    CHECK_TOK(tok, TOK_RWORD_FN);
    GET_CHECK_TOK(tok, lex, TOK_IDENT);
    // TODO: Hygine on function names? - Not in impl blocks?
    auto name = tok.ident().name;
    DEBUG("Function " << name);
    // - Self allowed, can't be prototype-form
    auto fcn = Parse_FunctionDefWithCode(lex, /*allow_self=*/true, std::move(abi), fn_flags);
    impl.add_function(lex.end_span(ps), mv$(item_attrs), vis, is_specialisable, mv$(name), mv$(fcn));
}

AST::Named<AST::Item> Parse_ExternBlock_Item(TokenStream& lex, const std::string& abi) {
    Token tok;
    auto meta_items = Parse_ItemAttrs(lex);
    SET_ATTRS(lex, meta_items);

    auto ps = lex.start_span();

    {
        ::AST::MacroInvocation inv;
        if (Parse_MacroInvocation_Opt(lex, inv)) {
            return ::AST::Named<::AST::Item>{lex.end_span(ps), mv$(meta_items), AST::Visibility::make_global(), "", ::AST::Item(mv$(inv))};
        }
    }

    auto vis = Parse_Publicity(lex);
    if (GET_TOK(tok, lex) == TOK_IDENT) {
        if (tok.ident() == "safe") {
            // TODO: Check that the next token is TOK_RWORD_FN
        } else {
            PUTBACK(tok, lex);
        }
    } else {
        PUTBACK(tok, lex);
    }
    switch (GET_TOK(tok, lex)) {
        case TOK_RWORD_FN: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto name = tok.ident().name;
            // parse function as prototype
            // - no self, is prototype, is unsafe and not const
            auto i = ::AST::Item(Parse_FunctionDef(lex, /*allow_self*/ false, /*can_be_prototype=*/true, abi, AST::Function::Flags::make_unsafe()));
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

            return AST::Named<AST::Item>{lex.end_span(ps), mv$(meta_items), vis, mv$(name), mv$(i)};
            break;
        }
        case TOK_RWORD_STATIC: {
            bool is_mut = false;
            if (GET_TOK(tok, lex) == TOK_RWORD_MUT) {
                is_mut = true;
            } else {
                PUTBACK(tok, lex);
            }
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto name = tok.ident().name;
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto type = Parse_Type(lex);
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

            auto i = ::AST::Item(::AST::Static((is_mut ? ::AST::Static::MUT : ::AST::Static::STATIC), mv$(type), ::AST::Expr()));
            return AST::Named<AST::Item>{lex.end_span(ps), mv$(meta_items), vis, mv$(name), mv$(i)};
            break;
        }
        case TOK_RWORD_TYPE: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto name = tok.ident().name;
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
            auto sp = lex.end_span(ps);
            //TODO(sp, "Extern type");
            auto i = ::AST::Item(::AST::TypeAlias(::AST::GenericParams(), ::TypeRef(sp)));
            return AST::Named<AST::Item>{mv$(sp), mv$(meta_items), vis, mv$(name), mv$(i)};
            break;
        }
        default:
            throw ParseError::Unexpected(lex, tok, {TOK_RWORD_FN, TOK_RWORD_STATIC, TOK_RWORD_TYPE});
    }
}

AST::ExternBlock Parse_ExternBlock(TokenStream& lex, ::std::string abi, ::AST::AttributeList& block_attrs) {
    TRACE_FUNCTION;
    Token tok;

    Parse_ParentAttrs(lex, block_attrs);

    AST::ExternBlock rv{abi};

    while (GET_TOK(tok, lex) != TOK_BRACE_CLOSE) {
        PUTBACK(tok, lex);

        rv.add_item(Parse_ExternBlock_Item(lex, abi));
    }

    return rv;
}

RcString get_optional_ident(TokenStream& lex) {
    Token tok;
    GET_TOK(tok, lex);
    if (tok.type() == TOK_UNDERSCORE) {
        static unsigned anon_index = 0;
        return RcString::new_interned(FMT(" " << anon_index++));
        //return RcString::new_interned(FMT(" " << lex.parse_state().module->m_anon_ident_index++));
    } else if (tok.type() == TOK_IDENT) {
        return tok.ident().name;
    } else {
        throw ParseError::Unexpected(lex, tok, {TOK_UNDERSCORE, TOK_IDENT});
    }
}

/// Parse multiple items from a use "statement"
void Parse_Use_Inner(TokenStream& lex, ::std::vector<AST::UseItem::Ent>& entries, AST::Path& path) {
    TRACE_FUNCTION_FR(path, entries);
    Token tok;

    while (lex.getTokenIf(TOK_RWORD_SUPER)) {
        lex.getTokenCheck(TOK_DOUBLE_COLON);
        if (auto* p = path.m_class.opt_Super()) {
            if (p->nodes.empty()) {
                p->count += 1;
            } else {
                p->nodes.pop_back();
            }
        } else {
            ASSERT_BUG(lex.point_span(), path.nodes().size() > 0, "super in empty path");
            path.nodes().pop_back();
        }
    }

    do {
        switch (GET_TOK(tok, lex)) {
            case TOK_IDENT:
                path.append(AST::PathNode(tok.ident().name, {}));
                break;
            case TOK_BRACE_OPEN:
                // Can't be an empty list
                if (LOOK_AHEAD(lex) == TOK_BRACE_CLOSE) {
                    throw ParseError::Unexpected(lex, tok);
                }
                // Keep looping until a comma
                do {
                    if (lex.getTokenIf(TOK_BRACE_CLOSE, tok)) {
                        // Trailing comma
                        break;
                    }
                    // - Handle `self` in braces differently
                    else if (lex.getTokenIf(TOK_RWORD_SELF)) {
                        RcString name;
                        if (lex.getTokenIf(TOK_RWORD_AS)) {
                            name = get_optional_ident(lex);
                        } else {
                            if (path.nodes().size() == 0) {
                                ERROR(lex.point_span(), E0000, "`self` with no path, use `as` to give it a name");
                            }
                            name = path.nodes().back().name();
                        }
                        entries.push_back({lex.point_span(), AST::Path(path), ::std::move(name)});
                    } else {
                        auto saved_path = AST::Path(path);

                        Parse_Use_Inner(lex, entries, path);

                        path = std::move(saved_path);
                    }
                } while (GET_TOK(tok, lex) == TOK_COMMA);
                CHECK_TOK(tok, TOK_BRACE_CLOSE);
                return;
            case TOK_STAR:
                entries.push_back({lex.point_span(), AST::Path(path), ""});
                return;
            default:
                throw ParseError::Unexpected(lex, tok);
        }
    } while (GET_TOK(tok, lex) == TOK_DOUBLE_COLON);

    RcString name;

    // NOTE: The above loop has to run once, so the last token HAS to have been an ident
    if (tok.type() == TOK_RWORD_AS) {
        name = get_optional_ident(lex);
    } else {
        PUTBACK(tok, lex);
        ASSERT_BUG(lex.point_span(), path.nodes().size() > 0, "`use` with no path");
        name = path.nodes().back().name();
    }

    // TODO: Get a span covering the final node.
    entries.push_back({lex.point_span(), AST::Path(path), ::std::move(name)});
}

void Parse_Use_Root(TokenStream& lex, ::std::vector<AST::UseItem::Ent>& entries) {
    AST::Path path = AST::Path("", {});
    Token tok;
    switch (GET_TOK(tok, lex)) {
        case TOK_RWORD_SELF:
            path = AST::Path::new_self({}); // relative path
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            break;
        case TOK_RWORD_SUPER: {
            unsigned int count = 1;
            while (LOOK_AHEAD(lex) == TOK_DOUBLE_COLON && lex.lookahead(1) == TOK_RWORD_SUPER) {
                GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
                GET_CHECK_TOK(tok, lex, TOK_RWORD_SUPER);
                count += 1;
            }
            path = AST::Path::new_super(count, {});
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            break;
        }
        case TOK_RWORD_CRATE:
            if (lex.lookahead(0) == TOK_RWORD_AS) {
                GET_CHECK_TOK(tok, lex, TOK_RWORD_AS);
                auto name = get_optional_ident(lex);
                entries.push_back({lex.point_span(), AST::Path(path), ::std::move(name)});
                return;
            }
            // 1.29 absolute path
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            break;
            // Leading :: is allowed and ignored for the $crate feature
        case TOK_DOUBLE_COLON:
            // Absolute path
            // HACK! mrustc emits $crate as `::"crate-name"`
            if (LOOK_AHEAD(lex) == TOK_STRING) {
                GET_CHECK_TOK(tok, lex, TOK_STRING);
                path = ::AST::Path(RcString::new_interned(tok.str()), {});
            } else if (lex.edition_after(AST::Edition::Rust2018)) {
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                // HACK: if the crate name starts with `=` it's a 2018 absolute path (references a crate loaded with `--extern`)
                path = ::AST::Path(RcString(std::string("=") + tok.ident().name.c_str()), {});
                // TODO: Is `use ::foo as bar` valid?
                if (lex.lookahead(0) != TOK_DOUBLE_COLON) {
                    RcString name;
                    if (lex.lookahead(0) == TOK_RWORD_AS) {
                        GET_CHECK_TOK(tok, lex, TOK_RWORD_AS);
                        name = get_optional_ident(lex);
                    } else {
                        name = path.m_class.as_Absolute().crate.c_str() + 1;
                    }

                    // TODO: Get a span covering the final node.
                    entries.push_back({lex.point_span(), AST::Path(path), ::std::move(name)});
                    return;
                }
            } else {
                PUTBACK(tok, lex);
            }
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            break;
        case TOK_INTERPOLATED_TYPE: {
            if (!tok.frag_type().is_path()) {
                throw ParseError::Unexpected(lex, tok);
            }
            auto& p = tok.frag_type().path();
            if (p.m_class.is_UFCS()) {
                throw ParseError::Unexpected(lex, tok);
            }
            path = std::move(tok.frag_type().path());
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
        } break;
        case TOK_INTERPOLATED_PATH:
            path = mv$(tok.frag_path());
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            break;
        default:
            if (lex.edition_after(AST::Edition::Rust2018)) {
                //path = AST::Path(lex.parse_state().module->path());
                path = AST::Path::new_relative(/*hygine=*/{}, {});
            }
            PUTBACK(tok, lex);
            break;
    }

    Parse_Use_Inner(lex, entries, path);
}

::AST::UseItem Parse_Use(TokenStream& lex) {
    TRACE_FUNCTION;

    Token tok;
    ProtoSpan span_start = lex.start_span();

    ::std::vector<AST::UseItem::Ent> entries;

    if (lex.lookahead(0) == TOK_BRACE_OPEN) {
        GET_TOK(tok, lex);
        do {
            if (lex.lookahead(0) == TOK_BRACE_CLOSE) {
                GET_TOK(tok, lex);
                break;
            }
            Parse_Use_Root(lex, entries);
        } while (GET_TOK(tok, lex) == TOK_COMMA);
        CHECK_TOK(tok, TOK_BRACE_CLOSE);
    } else {
        Parse_Use_Root(lex, entries);
    }

    return AST::UseItem{lex.end_span(span_start), mv$(entries)};
}

::AST::MacroInvocation Parse_MacroInvocation(ProtoSpan span_start, AST::Path name, TokenStream& lex) {
    Token tok;
    RcString ident;
    if (GET_TOK(tok, lex) == TOK_IDENT) {
        ident = tok.ident().name;
    } else {
        PUTBACK(tok, lex);
    }
    bool is_macro = (name.is_trivial() && name.as_trivial() == "macro_rules");

    if (is_macro) {
        lex.push_hygine();
    }
    TokenTree tt = Parse_TT(lex, true);
    if (tt.is_token()) {
        throw ParseError::Unexpected(lex, tt.tok());
    }
    if (is_macro) {
        lex.pop_hygine();
    }
    DEBUG("name=" << name << ", ident=" << ident << ", tt=" << tt);
    return ::AST::MacroInvocation(lex.end_span(span_start), mv$(name), mv$(ident), mv$(tt));
}

bool Parse_MacroInvocation_Opt(TokenStream& lex, AST::MacroInvocation& out_inv) {
    Token tok;

    switch (lex.lookahead(0)) {
        case TOK_IDENT:
            if (!(lex.lookahead(1) == TOK_DOUBLE_COLON || lex.lookahead(1) == TOK_EXCLAM)) {
                return false;
            }
            break;
        case TOK_RWORD_CRATE:
            if (!(lex.lookahead(1) == TOK_DOUBLE_COLON)) {
                return false;
            }
            break;
        case TOK_DOUBLE_COLON:
        case TOK_RWORD_SUPER:
        case TOK_RWORD_SELF:
        case TOK_INTERPOLATED_PATH:
            break;
        default:
            return false;
    }

    auto ps = lex.start_span();
    auto name_path = Parse_Path(lex, PATH_GENERIC_NONE);
    GET_CHECK_TOK(tok, lex, TOK_EXCLAM);

    bool is_braced = (lex.lookahead(0) == TOK_BRACE_OPEN || (lex.lookahead(0) == TOK_IDENT && lex.lookahead(1) == TOK_BRACE_OPEN));

    out_inv = Parse_MacroInvocation(ps, name_path, lex);

    if (!is_braced) {
        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
    }
    return true;
}


::AST::Named<::AST::Item> Parse_Mod_Item_S(TokenStream& lex, const AST::Module::FileInfo& mod_fileinfo, const ::AST::AbsolutePath& mod_path, AST::AttributeList meta_items) {
    TRACE_FUNCTION_F("mod_path=" << mod_path << ", meta_items=" << meta_items);
    Token tok;

    // NOTE: This assigns into a parameter, so can't use Parse_ItemAttrs
    while (LOOKAHEAD2(lex, TOK_HASH, TOK_SQUARE_OPEN)) {
        // Attributes!
        GET_CHECK_TOK(tok, lex, TOK_HASH);
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_OPEN);
        meta_items.push_back(Parse_MetaItem(lex));
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
    }

    if (LOOK_AHEAD(lex) == TOK_INTERPOLATED_ITEM) {
        GET_TOK(tok, lex);
        auto rv = tok.take_frag_item();
        // Transfer new attributes onto the item
        for (auto& mi : meta_items.m_items) {
            rv.attrs.m_items.push_back(mv$(mi));
        }
        return rv;
    }

    auto ps = lex.start_span();

    {
        ::AST::MacroInvocation inv;
        if (Parse_MacroInvocation_Opt(lex, inv)) {
            return ::AST::Named<::AST::Item>{lex.end_span(ps), mv$(meta_items), AST::Visibility::make_global(), "", ::AST::Item(mv$(inv))};
        }
    }

    RcString item_name;
    ::AST::Item item_data;

    auto vis = Parse_Publicity(lex);

    switch (GET_TOK(tok, lex)) {
        case TOK_RWORD_USE:
            item_data = Parse_Use(lex);
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
            break;

        case TOK_RWORD_EXTERN:
            switch (GET_TOK(tok, lex)) {
                // `extern "<ABI>" fn ...`
                // `extern "<ABI>" { ...`
                case TOK_STRING: {
                    ::std::string abi = tok.str();
                    switch (GET_TOK(tok, lex)) {
                        // `extern "<ABI>" fn ...`
                        case TOK_RWORD_FN: {
                            GET_CHECK_TOK(tok, lex, TOK_IDENT);
                            item_name = tok.ident().name;
                            item_data = ::AST::Item(Parse_FunctionDefWithCode(lex, /*allow_self=*/false, abi, AST::Function::Flags()));
                            break;
                        }
                        // `extern "ABI" {`
                        case TOK_BRACE_OPEN:
                            item_name = "";
                            item_data = ::AST::Item(Parse_ExternBlock(lex, mv$(abi), meta_items));
                            break;
                        default:
                            throw ParseError::Unexpected(lex, tok, {TOK_RWORD_FN, TOK_BRACE_OPEN});
                    }
                    break;
                }
                // `extern fn ...`
                case TOK_RWORD_FN:
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    item_name = tok.ident().name;
                    item_data = ::AST::Item(Parse_FunctionDefWithCode(lex, /*allow_self=*/false, "C", AST::Function::Flags()));
                    break;

                // NOTE: `extern { ...` is handled in caller
                case TOK_BRACE_OPEN:
                    item_name = "";
                    item_data = ::AST::Item(Parse_ExternBlock(lex, "C", meta_items));
                    break;

                // `extern crate "crate-name" as crate_name;`
                // `extern crate crate_name;`
                // `extern crate crate_name as other_name;`
                case TOK_RWORD_CRATE:
                    switch (GET_TOK(tok, lex)) {
                        case TOK_RWORD_SELF:
                            item_data = ::AST::Item::make_Crate({RcString::new_interned("")});
                            GET_CHECK_TOK(tok, lex, TOK_RWORD_AS);
                            GET_CHECK_TOK(tok, lex, TOK_IDENT);
                            item_name = tok.ident().name;
                            break;
                        // `extern crate "crate-name" as crate_name;`
                        // NOTE: rustc doesn't allow this, keep in mrustc for for reparse support
                        case TOK_STRING:
                            item_data = ::AST::Item::make_Crate({RcString::new_interned(tok.str())});
                            GET_CHECK_TOK(tok, lex, TOK_RWORD_AS);
                            GET_CHECK_TOK(tok, lex, TOK_IDENT);
                            item_name = tok.ident().name;
                            break;
                        // `extern crate crate_name;`
                        // `extern crate crate_name as other_name;`
                        case TOK_IDENT:
                            item_name = tok.ident().name;
                            if (GET_TOK(tok, lex) == TOK_RWORD_AS) {
                                item_data = ::AST::Item::make_Crate({mv$(item_name)});

                                item_name = get_optional_ident(lex);
                            } else {
                                PUTBACK(tok, lex);
                                item_data = ::AST::Item::make_Crate({item_name});
                            }
                            break;
                        default:
                            throw ParseError::Unexpected(lex, tok, {TOK_STRING, TOK_IDENT});
                    }
                    GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
                    break;
                default:
                    throw ParseError::Unexpected(lex, tok, {TOK_STRING, TOK_RWORD_FN, TOK_BRACE_OPEN, TOK_RWORD_CRATE});
            }
            break;

        // `const NAME`
        // `const [unsafe] fn`
        case TOK_RWORD_CONST:
            switch (GET_TOK(tok, lex)) {
                case TOK_UNDERSCORE: // 1.39?
                case TOK_IDENT: {
                    PUTBACK(tok, lex);
                    item_name = get_optional_ident(lex);

                    GET_CHECK_TOK(tok, lex, TOK_COLON);
                    TypeRef type = Parse_Type(lex);
                    GET_CHECK_TOK(tok, lex, TOK_EQUAL);
                    AST::Expr val = Parse_Expr(lex);
                    GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
                    item_data = ::AST::Item(::AST::Static(AST::Static::CONST, mv$(type), mv$(val)));
                    break;
                }
                case TOK_RWORD_UNSAFE: {
                    struct H {
                        static std::string opt_extern(Token& tok, TokenStream& lex) {
                            if (lex.lookahead(0) == TOK_RWORD_EXTERN) {
                                GET_TOK(tok, lex);
                                if (lex.lookahead(0) == TOK_STRING) {
                                    GET_TOK(tok, lex);
                                    return tok.str();
                                } else {
                                    return "C";
                                }
                            } else {
                                return ABI_RUST;
                            }
                        }
                    };

                    auto abi = H::opt_extern(tok, lex);
                    GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    item_name = tok.ident().name;
                    item_data = ::AST::Item(Parse_FunctionDefWithCode(lex, /*allow_self=*/false, abi, AST::Function::Flags().set_const().set_unsafe()));
                    break;
                }
                case TOK_RWORD_EXTERN: {
                    auto abi = lex.lookahead(0) == TOK_STRING ? lex.getToken().str() : "C";
                    GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    item_name = tok.ident().name;
                    item_data = ::AST::Item(Parse_FunctionDefWithCode(lex, /*allow_self=*/false, abi, AST::Function::Flags().set_const()));
                    break;
                }
                case TOK_RWORD_FN:
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    item_name = tok.ident().name;
                    // - self not allowed, not prototype
                    item_data = ::AST::Item(Parse_FunctionDefWithCode(lex, /*allow_self=*/false, ABI_RUST, AST::Function::Flags().set_const()));
                    break;
                default:
                    throw ParseError::Unexpected(lex, tok, {TOK_IDENT, TOK_UNDERSCORE, TOK_RWORD_UNSAFE, TOK_RWORD_FN});
            }
            break;
        // `static NAME`
        // `static mut NAME`
        case TOK_RWORD_STATIC: {
            bool is_mut = false;
            if (GET_TOK(tok, lex) == TOK_RWORD_MUT) {
                is_mut = true;
                GET_TOK(tok, lex);
            }
            PUTBACK(tok, lex);
            item_name = get_optional_ident(lex);

            GET_CHECK_TOK(tok, lex, TOK_COLON);
            TypeRef type = Parse_Type(lex);

            GET_CHECK_TOK(tok, lex, TOK_EQUAL);

            AST::Expr val = Parse_Expr(lex);

            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
            item_data = ::AST::Item(::AST::Static((is_mut ? AST::Static::MUT : AST::Static::STATIC), mv$(type), mv$(val)));
            break;
        }

        // `unsafe fn`
        // `unsafe trait`
        // `unsafe impl`
        case TOK_RWORD_UNSAFE:
            switch (GET_TOK(tok, lex)) {
                // `unsafe extern fn`
                case TOK_RWORD_EXTERN: {
                    ::std::string abi = "C";
                    if (GET_TOK(tok, lex) == TOK_STRING) {
                        abi = mv$(tok.str());
                    } else {
                        PUTBACK(tok, lex);
                    }
                    if (lex.getTokenIf(TOK_BRACE_OPEN)) {
                        item_name = "";
                        item_data = ::AST::Item(Parse_ExternBlock(lex, "C", meta_items));
                    } else {
                        GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
                        GET_CHECK_TOK(tok, lex, TOK_IDENT);
                        item_name = tok.ident().name;
                        item_data = ::AST::Item(Parse_FunctionDefWithCode(lex, false, abi, AST::Function::Flags().set_unsafe()));
                    }
                    break;
                }
                // `unsafe fn`
                case TOK_RWORD_FN:
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    item_name = tok.ident().name;
                    // - self not allowed, not prototype
                    item_data = ::AST::Item(Parse_FunctionDefWithCode(lex, false, ABI_RUST, AST::Function::Flags().set_unsafe()));
                    break;
                // `unsafe trait`
                case TOK_RWORD_TRAIT: {
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    item_name = tok.ident().name;
                    auto tr = Parse_TraitDef(lex, meta_items, Parse_GenericParamsOpt(lex));
                    tr.set_is_unsafe();
                    item_data = ::AST::Item(::std::move(tr));
                    break;
                }
                // `unsafe impl`
                case TOK_RWORD_IMPL: {
                    auto impl = Parse_Impl(lex, meta_items, true);
                    if (impl.is_Impl()) {
                        impl.as_Impl().def().set_is_unsafe();
                    } else if (impl.is_NegImpl()) {
                        impl.as_NegImpl().set_is_unsafe();
                    } else {
                        BUG(lex.point_span(), "Parse_Impl returned a variant other than Impl or NegImpl");
                    }
                    return ::AST::Named<::AST::Item>{Span(), mv$(meta_items), AST::Visibility::make_global(), "", mv$(impl)};
                }
                // `unsafe auto trait`
                case TOK_IDENT:
                    if (tok.ident().name == "auto") {
                        GET_CHECK_TOK(tok, lex, TOK_RWORD_TRAIT);
                        GET_CHECK_TOK(tok, lex, TOK_IDENT);
                        item_name = tok.ident().name;
                        auto tr = Parse_TraitDef(lex, meta_items, Parse_GenericParamsOpt(lex));
                        tr.set_is_unsafe();
                        tr.set_is_marker();
                        item_data = ::AST::Item(::std::move(tr));
                        break;
                    }
                    //goto default;
                default:
                    throw ParseError::Unexpected(lex, tok, {TOK_RWORD_FN, TOK_RWORD_TRAIT, TOK_RWORD_IMPL});
            }
            break;
        case TOK_RWORD_ASYNC: {
            AST::Function::Flags flags;
            flags.is_async = true;
            ;
            if (lex.getTokenIf(TOK_RWORD_UNSAFE)) {
                flags.is_unsafe = true;
            }
            GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            item_name = tok.ident().name;
            // - self not allowed, not prototype
            item_data = ::AST::Item(Parse_FunctionDefWithCode(lex, false, ABI_RUST, flags));
            break;
        }
        // `fn`
        case TOK_RWORD_FN:
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            item_name = tok.ident().name;
            // - self not allowed, not prototype
            item_data = ::AST::Item(Parse_FunctionDefWithCode(lex, false, ABI_RUST, AST::Function::Flags()));
            break;
        // `type`
        case TOK_RWORD_TYPE:
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            item_name = tok.ident().name;
            item_data = ::AST::Item(Parse_TypeAlias(lex));
            break;
        // `struct`
        case TOK_RWORD_STRUCT:
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            item_name = tok.ident().name;
            item_data = ::AST::Item(Parse_Struct(lex, meta_items));
            break;
        // `enum`
        case TOK_RWORD_ENUM:
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            item_name = tok.ident().name;
            item_data = ::AST::Item(Parse_EnumDef(lex, meta_items));
            break;

        // Contextual keywords
        case TOK_IDENT:
            if (tok.ident().name == "union") {
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                item_name = tok.ident().name;
                item_data = ::AST::Item(Parse_Union(lex, meta_items));
            }
            // `auto trait`
            else if (tok.ident().name == "auto") {
                GET_CHECK_TOK(tok, lex, TOK_RWORD_TRAIT);
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                item_name = tok.ident().name;
                auto tr = Parse_TraitDef(lex, meta_items, Parse_GenericParamsOpt(lex));
                tr.set_is_marker();
                item_data = ::AST::Item(::std::move(tr));
            } else {
                throw ParseError::Unexpected(lex, tok);
            }
            break;

        // `impl`
        case TOK_RWORD_IMPL: {
            auto impl = Parse_Impl(lex, meta_items);
            return ::AST::Named<::AST::Item>{Span(), std::move(meta_items), AST::Visibility::make_global(), "", std::move(impl)};
        }
        // `trait`
        case TOK_RWORD_TRAIT: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            item_name = tok.ident().name;
            AST::GenericParams params = Parse_GenericParamsOpt(lex);
            if (lex.lookahead(0) == TOK_EQUAL) {
                // Trait alias (can't be auto or unsafe?)

                AST::TraitAlias rv;
                rv.params = std::move(params);
                do {
                    lex.getToken();

                    auto ps = lex.start_span();
                    auto hrbs = Parse_HRB_Opt(lex);
                    rv.traits.push_back(GET_SPANNED(Type_TraitPath, lex, (Type_TraitPath(mv$(hrbs), Parse_Path(lex, PATH_GENERIC_TYPE)))));
                } while (lex.lookahead(0) == TOK_PLUS);

                GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

                item_data = ::AST::Item(std::move(rv));
            } else {
                item_data = ::AST::Item(Parse_TraitDef(lex, meta_items, std::move(params)));
            }
            break;
        }

        case TOK_RWORD_MACRO:
            {
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                auto name = tok.ident().name;
                DEBUG("name = " << name);
                MacroRulesPtr mrp;
                if (lex.lookahead(0) == TOK_BRACE_OPEN) {
                    GET_TOK(tok, lex);
                    mrp = Parse_MacroRules(lex);
                } else if (lex.lookahead(0) == TOK_PAREN_OPEN) {
                    mrp = Parse_MacroRulesSingleArm(lex);
                } else {
                    GET_TOK(tok, lex);
                    throw ParseError::Unexpected(lex, tok);
                }

                {
                    Ident::ModPath mp;
                    mp.crate = "";
                    mp.ents = mod_path.nodes;
                    mrp->m_hygiene.set_mod_path(::std::move(mp));
                    mrp->m_is_macro_item = true;
                }

                item_name = name;
                item_data = ::AST::Item(mv$(mrp));
            }
            break;

        case TOK_RWORD_MOD: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto name = tok.ident().name;
            DEBUG("Sub module '" << name << "'");
            AST::Module submod(mod_path + name);

            // Check #[cfg] and don't load if it fails
            struct H {
                static bool check_item_cfg(const ::AST::AttributeList& attrs) {
                    for (const auto& at : attrs.m_items) {
                        if (at.name() == "cfg" && !check_cfg(at.span(), at)) {
                            return false;
                        }
                    }
                    return true;
                }
            };

            // Rules for external files (/ path handling):
            // - IF using stdin (path='-') - Disallow and propagate '-' as path
            // - IF a #[path] attribute was passed, allow
            // - IF in crate root or mod.rs, allow (input flag)
            // - else, disallow and set flag
            ::std::string path_attr;
            for (const auto& a : meta_items.m_items) {
                DEBUG("[mod path_attr] " << a);
                if (a.name() == "path") {
                    path_attr = a.parse_equals_string(*lex.parse_state().crate, *lex.parse_state().module);
                } else if (a.name() == "cfg_attr") {
                    for (const auto& a2 : check_cfg_attr(a)) {
                        DEBUG("[mod path_attr cfg_attr] " << a2);
                        if (a2.name() == "path") {
                            path_attr = a2.parse_equals_string(*lex.parse_state().crate, *lex.parse_state().module);
                        }
                    }
                } else {
                }
            }
            DEBUG("path_attr = \"" << path_attr << "\"");

            //submod.m_file_info = get_submod_file(lex.end_span(ps), mod_fileinfo,  name, path_attr, LOOK_AHEAD(lex) == TOK_SEMICOLON, H::check_item_cfg(meta_items));

            ::helpers::path sub_path;
            bool sub_file_controls_dir = true;
            if (mod_fileinfo.path == "-") {
                if (path_attr.size()) {
                    ERROR(lex.point_span(), E0000, "Cannot load module from file when reading stdin");
                }
                sub_path = "-";
            } else if (path_attr.size() > 0) {
                // If in a local mod, then use this arm
                bool in_submod = mod_fileinfo.path[mod_fileinfo.path.size() - 1] == '/';
                if (mod_fileinfo.in_mod_block) {
                    // REF: `rustc-1.90.0-src/vendor/hashbrown-0.14.5/src/lib.rs:63`
                    sub_path = dirname(mod_fileinfo.path) / path_attr.c_str();
                } else {
                    // Otherwise use this:
                    // REF: `rustc-1.90.0-src/vendor/icu_list_data-1.5.1/data/macros.rs:30`
                    sub_path = dirname(lex.point_span().get_top_file_span().filename.c_str()) / path_attr.c_str();
                }
            } else if (mod_fileinfo.controls_dir) {
                sub_path = dirname(mod_fileinfo.path) / name.c_str();
            } else {
                sub_path = dirname(mod_fileinfo.path) / mod_path.nodes.back().c_str() / name.c_str();
                //sub_path = mod_fileinfo.path;
                sub_file_controls_dir = false;
            }
            DEBUG("Mod '" << name << "', sub_path = " << sub_path);

            submod.m_file_info.path = sub_path;
            submod.m_file_info.controls_dir = sub_file_controls_dir;

            switch (GET_TOK(tok, lex)) {
                case TOK_BRACE_OPEN:
                    submod.m_file_info.path = sub_path.str() + "/";
                    submod.m_file_info.in_mod_block = true;
                    submod.m_file_info.is_disabled = !H::check_item_cfg(meta_items);
                    // TODO: If cfg fails, just eat the TT until a matching #[cfg]?
                    // - Or, mark the file infor as not being valid (so child modules don't try to load)
                    Parse_ModRoot(lex, submod, meta_items);
                    GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);
                    break;
                case TOK_SEMICOLON:
                    if (mod_fileinfo.is_disabled) {
                    } else if (sub_path.str() == "-") {
                        ERROR(lex.point_span(), E0000, "Cannot load module from file when reading stdin");
                    } else if (!H::check_item_cfg(meta_items)) {
                        // Ignore - emit Item::None
                        item_name = mv$(name);
                        item_data = ::AST::Item();
                        break;
                    } else if (path_attr.size() == 0 && !mod_fileinfo.controls_dir) {
                        ASSERT_BUG(lex.point_span(), mod_path.nodes.size() >= 1, "Crate root should control its directory?");
                        // Look for `curdir/curmod/submod.rs` or `curdir/curmod/submod/mod.rs`
                        ::std::string newpath_file_direct = dirname(mod_fileinfo.path) / mod_path.nodes.back().c_str() / name.c_str() + ".rs";
                        ::std::string newpath_file_mod = dirname(mod_fileinfo.path) / mod_path.nodes.back().c_str() / name.c_str() / "mod.rs";
                        DEBUG(mod_fileinfo.path << " " << mod_path);
                        DEBUG("newpath_file_direct = '" << newpath_file_direct << "'");
                        DEBUG("newpath_file_mod = '" << newpath_file_mod << "'");

                        ::std::ifstream ifs_file(newpath_file_direct);
                        ::std::ifstream ifs_dir(newpath_file_mod);

                        if (ifs_dir.is_open() && ifs_file.is_open()) {
                            // Collision
                            ERROR(lex.point_span(), E0000, "Both modname.rs and modname/mod.rs exist");
                        } else if (ifs_dir.is_open()) {
                            // Load from dir
                            submod.m_file_info.path = newpath_file_mod;
                            submod.m_file_info.controls_dir = true;
                        } else if (ifs_file.is_open()) {
                            submod.m_file_info.path = newpath_file_direct;
                            submod.m_file_info.controls_dir = false;
                        } else {
                            // Can't find file
                            ERROR(lex.point_span(), E0000, "Can't find file for '" << name << "' in '" << mod_fileinfo.path << "'");
                        }
                        DEBUG("- path = " << submod.m_file_info.path);
                        Lexer sub_lex(submod.m_file_info.path, lex.get_edition(), lex.parse_state());
                        Parse_ModRoot(sub_lex, submod, meta_items);
                        GET_CHECK_TOK(tok, sub_lex, TOK_EOF);
                    } else {
                        ::std::string newpath_dir = sub_path.str() + "/";
                        ::std::string newpath_file = path_attr.size() > 0 ? sub_path : sub_path + ".rs";
                        DEBUG("newpath_dir = '" << newpath_dir << "', newpath_file = '" << newpath_file << "'");
                        ::std::ifstream ifs_dir(newpath_dir + "mod.rs");
                        ::std::ifstream ifs_file(newpath_file);
                        if (ifs_dir.is_open() && ifs_file.is_open()) {
                            // Collision
                            ERROR(lex.point_span(), E0000, "Both modname.rs and modname/mod.rs exist");
                        } else if (ifs_dir.is_open()) {
                            // Load from dir
                            submod.m_file_info.path = newpath_dir + "mod.rs";
                        } else if (ifs_file.is_open()) {
                            submod.m_file_info.path = newpath_file;
                            if (path_attr == "") {
                                submod.m_file_info.controls_dir = false;
                            }
                        }
                        // TODO: If this is not a controlling file, look in `modname/` for the new module
                        else {
                            // Can't find file
                            ERROR(lex.point_span(), E0000, "Can't find file for '" << name << "' in '" << mod_fileinfo.path << "'");
                        }
                        DEBUG("- path = " << submod.m_file_info.path);
                        Lexer sub_lex(submod.m_file_info.path, lex.get_edition(), lex.parse_state());
                        Parse_ModRoot(sub_lex, submod, meta_items);
                        GET_CHECK_TOK(tok, sub_lex, TOK_EOF);
                    }
                    break;
                default:
                    throw ParseError::Generic("Expected { or ; after module name");
            }
            item_name = mv$(name);
            item_data = ::AST::Item(mv$(submod));
            break;
        }

        default:
            throw ParseError::Unexpected(lex, tok);
    }

    return ::AST::Named<::AST::Item>{lex.end_span(ps), mv$(meta_items), vis, mv$(item_name), mv$(item_data)};
}

void Parse_Mod_Item(TokenStream& lex, AST::Module& mod, AST::AttributeList meta_items) {
    SET_MODULE(lex, mod);
    lex.parse_state().module = &mod;
    lex.parse_state().parent_attrs = &meta_items;

    mod.add_item(Parse_Mod_Item_S(lex, mod.m_file_info, mod.path(), mv$(meta_items)));
}

void Parse_ModRoot_Items(TokenStream& lex, AST::Module& mod) {
    Token tok;

    for (;;) {
        // Check 1 - End of module (either via a closing brace, or EOF)
        switch (GET_TOK(tok, lex)) {
            case TOK_BRACE_CLOSE:
            case TOK_EOF:
                PUTBACK(tok, lex);
                return;
            default:
                PUTBACK(tok, lex);
                break;
        }

        // Attributes on the following item
        auto meta_items = Parse_ItemAttrs(lex);
        DEBUG("meta_items = " << meta_items);

        Parse_Mod_Item(lex, mod, mv$(meta_items));
    }
}

void Parse_ModRoot(TokenStream& lex, AST::Module& mod, AST::AttributeList& mod_attrs) {
    TRACE_FUNCTION;

    auto prev_mod = lex.parse_state().module;
    lex.parse_state().module = &mod;
    // Attributes on module/crate (will continue loop)
    Parse_ParentAttrs(lex, mod_attrs);

    Parse_ModRoot_Items(lex, mod);
    lex.parse_state().module = prev_mod;
}

AST::Crate* Parse_Crate(stl::ObjPool* pool, HIR::TypeInterner& types, ::std::string mainfile, AST::Edition edition) {
    Token tok;

    Lexer lex(mainfile, edition, ParseState());

    size_t p = mainfile.find_last_of('/');
    p = (p == ::std::string::npos ? mainfile.find_last_of('\\') : p);
    ::std::string mainpath = mainfile == "-" ? "-" : (p != ::std::string::npos ? ::std::string(mainfile.begin(), mainfile.begin() + p + 1) : "./");

    auto* crate = pool->make<AST::Crate>(pool, types);
    crate->m_edition = edition;

    //crate.root_module().m_file_info.file_path = mainfile;
    crate->root_module().m_file_info.path = mainpath;
    crate->root_module().m_file_info.controls_dir = true;

    lex.parse_state().crate = crate;
    Parse_ModRoot(lex, crate->root_module(), crate->m_attrs);

    return crate;
}

#undef GET_SPANNED
#undef LOOKAHEAD2

#include "parse_common.h"
#include "parse_parseerror.h"
#include "ast_types.h"
#include "ast_ast.h"

// === PROTOTYPES ===
//TypeRef Parse_Type(TokenStream& lex, bool allow_trait_list);
TypeRef Parse_Type_Int(TokenStream& lex, bool allow_trait_list);
TypeRef Parse_Type_Fn(TokenStream& lex, AST::HigherRankedBounds hrbs = {});
TypeRef Parse_Type_Path(TokenStream& lex, AST::HigherRankedBounds hrbs, bool allow_trait_list);
TypeRef Parse_Type_TraitObject(TokenStream& lex, ::AST::HigherRankedBounds hrbs = {});
TypeRef Parse_Type_ErasedType(TokenStream& lex, bool allow_trait_list);

// === CODE ===
TypeRef Parse_Type(TokenStream& lex, bool allow_trait_list) {
    //ProtoSpan ps = lex.start_span();
    TypeRef rv = Parse_Type_Int(lex, allow_trait_list);
    //rv.set_span(lex.end_span(ps));
    return rv;
}

TypeRef Parse_Type_Int(TokenStream& lex, bool allow_trait_list) {
    //TRACE_FUNCTION;
    auto ps = lex.start_span();

    Token tok;

    switch (GET_TOK(tok, lex)) {
        case TOK_INTERPOLATED_TYPE:
            return mv$(tok.frag_type());
        // '!' - Only ever used as part of function prototypes, but is kinda a type... not allowed here though
        case TOK_EXCLAM:
            return TypeRef(lex.sub_span(tok.get_pos()), TypeData::make_Bang({}));
        // '_' = Wildcard (type inferrence variable)
        case TOK_UNDERSCORE:
            return TypeRef(lex.sub_span(tok.get_pos()));

        // 'unsafe' - An unsafe function type
        case TOK_RWORD_UNSAFE:
        // 'extern' - A function type with an ABI
        case TOK_RWORD_EXTERN:
        // 'fn' - Rust function
        case TOK_RWORD_FN:
            PUTBACK(tok, lex);
            return Parse_Type_Fn(lex);

        case TOK_RWORD_IMPL:
            return Parse_Type_ErasedType(lex, allow_trait_list);

        // '<' - An associated type cast
        case TOK_LT:
        case TOK_THINARROW_LEFT:
        case TOK_DOUBLE_LT: {
            PUTBACK(tok, lex);
            auto path = Parse_Path(lex, PATH_GENERIC_TYPE);
            return TypeRef(TypeRef::TagPath(), lex.end_span(ps), mv$(path));
        }
        //
        case TOK_RWORD_FOR: {
            auto hrls = Parse_HRB(lex);
            switch (LOOK_AHEAD(lex)) {
                case TOK_RWORD_UNSAFE:
                case TOK_RWORD_EXTERN:
                case TOK_RWORD_FN:
                    return Parse_Type_Fn(lex, hrls);
                default:
                    return Parse_Type_Path(lex, hrls, true);
            }
        }
        case TOK_RWORD_DYN: {
            ::AST::HigherRankedBounds hrbs = Parse_HRB_Opt(lex);
            return Parse_Type_TraitObject(lex, mv$(hrbs));
        }
        // <ident> - Either a primitive, or a path
        case TOK_IDENT:
            // TODO: Only allow if the next token isn't `::` or `!`
            if (tok.ident().name == "dyn") {
                ::AST::HigherRankedBounds hrbs = Parse_HRB_Opt(lex);
                return Parse_Type_TraitObject(lex, mv$(hrbs));
            }
            // or a primitive
            //if( auto ct = coretype_fromstring(tok.str()) )
            //{
            //    return TypeRef(TypeRef::TagPrimitive(), Span(tok.get_pos()), ct);
            //}
            PUTBACK(tok, lex);
            return Parse_Type_Path(lex, {}, allow_trait_list);
            // - Fall through to path handling
        // '::' - Absolute path
        case TOK_DOUBLE_COLON:
        // 'self' - This relative path
        case TOK_RWORD_SELF:
        // 'super' - Parent relative path
        case TOK_RWORD_SUPER:
        // 'crate' - Crate-relative path
        case TOK_RWORD_CRATE:
        // ':path' fragment
        case TOK_INTERPOLATED_PATH:
            PUTBACK(tok, lex);
            return Parse_Type_Path(lex, {}, allow_trait_list);

        // HACK! Convert && into & &
        case TOK_DOUBLE_AMP:
            lex.putback(Token(TOK_AMP));
        // '&' - Reference type
        case TOK_AMP: {
            AST::LifetimeRef lifetime;
            // Reference
            tok = lex.getToken();
            if (tok.type() == TOK_LIFETIME) {
                lifetime = AST::LifetimeRef(/*lex.point_span(), */ tok.ident());
                tok = lex.getToken();
            }
            bool is_mut = false;
            if (tok.type() == TOK_RWORD_MUT) {
                is_mut = true;
            } else {
                PUTBACK(tok, lex);
            }
            return TypeRef(TypeRef::TagReference(), lex.end_span(ps), ::std::move(lifetime), is_mut, Parse_Type(lex, false));
        }
        // '*' - Raw pointer
        case TOK_STAR:
            // Pointer
            switch (GET_TOK(tok, lex)) {
                case TOK_RWORD_MUT:
                    // Mutable pointer
                    return TypeRef(TypeRef::TagPointer(), lex.end_span(ps), true, Parse_Type(lex, false));
                case TOK_RWORD_CONST:
                    // Immutable pointer
                    return TypeRef(TypeRef::TagPointer(), lex.end_span(ps), false, Parse_Type(lex, false));
                default:
                    throw ParseError::Unexpected(lex, tok, {TOK_RWORD_CONST, TOK_RWORD_MUT});
            }
            throw ParseError::BugCheck("Reached end of Parse_Type:STAR");
        // '[' - Array type
        case TOK_SQUARE_OPEN: {
            // Array
            TypeRef inner = Parse_Type(lex);
            if (GET_TOK(tok, lex) == TOK_SEMICOLON) {
                // Inferred size - unspecified
                if (lex.getTokenIf(TOK_UNDERSCORE)) {
                    GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                    return TypeRef(TypeRef::TagSizedArray(), lex.end_span(ps), mv$(inner), nullptr);
                } else {
                    // Sized array
                    AST::Expr array_size = Parse_Expr(lex);
                    GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                    return TypeRef(TypeRef::TagSizedArray(), lex.end_span(ps), mv$(inner), array_size.take_node());
                }
            } else if (tok.type() == TOK_SQUARE_CLOSE) {
                return TypeRef(TypeRef::TagUnsizedArray(), lex.end_span(ps), mv$(inner));
            } else {
                throw ParseError::Unexpected(lex, tok /*, "; or ]"*/);
            }
        }

        // '(' - Tuple (or lifetime bounded trait)
        case TOK_PAREN_OPEN: {
            DEBUG("Tuple");
            if (GET_TOK(tok, lex) == TOK_PAREN_CLOSE) {
                return TypeRef(TypeRef::TagTuple(), lex.end_span(ps), {});
            }
            PUTBACK(tok, lex);

            TypeRef inner = Parse_Type(lex, true);
            if (LOOK_AHEAD(lex) == TOK_PAREN_CLOSE) {
                // Type in parens, NOT a tuple
                GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                return inner;
            } else {
                ::std::vector<TypeRef> types;
                types.push_back(mv$(inner));
                while (GET_TOK(tok, lex) == TOK_COMMA) {
                    if (GET_TOK(tok, lex) == TOK_PAREN_CLOSE) {
                        break;
                    } else {
                        PUTBACK(tok, lex);
                    }
                    types.push_back(Parse_Type(lex));
                }
                CHECK_TOK(tok, TOK_PAREN_CLOSE);
                return TypeRef(TypeRef::TagTuple(), lex.end_span(ps), mv$(types));
            }
        }
        default:
            throw ParseError::Unexpected(lex, tok);
    }
    throw ParseError::BugCheck("Reached end of Parse_Type");
}

TypeRef Parse_Type_Fn(TokenStream& lex, ::AST::HigherRankedBounds hrbs) {
    auto ps = lex.start_span();
    TRACE_FUNCTION;
    Token tok;

    ::std::string abi = "";
    bool is_unsafe = false;

    GET_TOK(tok, lex);

    // `unsafe`
    if (tok.type() == TOK_RWORD_UNSAFE) {
        is_unsafe = true;
        GET_TOK(tok, lex);
    }
    // `exern`
    if (tok.type() == TOK_RWORD_EXTERN) {
        if (GET_TOK(tok, lex) == TOK_STRING) {
            abi = tok.str();
            if (abi == "") {
                ERROR(lex.point_span(), E0000, "Empty ABI");
            }
            GET_TOK(tok, lex);
        } else {
            abi = "C";
        }
    }
    // `fn`
    CHECK_TOK(tok, TOK_RWORD_FN);

    ::std::vector<TypeRef> args;
    bool is_variadic = false;
    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    while (LOOK_AHEAD(lex) != TOK_PAREN_CLOSE) {
        if (LOOK_AHEAD(lex) == TOK_TRIPLE_DOT) {
            GET_TOK(tok, lex);
            is_variadic = true;
            break;
        }
        // Handle `ident: `
        if ((lex.lookahead(0) == TOK_IDENT || lex.lookahead(0) == TOK_UNDERSCORE) && lex.lookahead(1) == TOK_COLON) {
            GET_TOK(tok, lex);
            GET_TOK(tok, lex);
        }
        args.push_back(Parse_Type(lex));
        if (GET_TOK(tok, lex) != TOK_COMMA) {
            PUTBACK(tok, lex);
            break;
        }
    }
    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);

    // `-> RetType`
    TypeRef ret_type = TypeRef(TypeRef::TagUnit(), lex.point_span());
    if (GET_TOK(tok, lex) == TOK_THINARROW) {
        ret_type = Parse_Type(lex, false);
    } else {
        PUTBACK(tok, lex);
    }

    return TypeRef(TypeRef::TagFunction(), lex.end_span(ps), mv$(hrbs), is_unsafe, mv$(abi), mv$(args), is_variadic, mv$(ret_type));
}

TypeRef Parse_Type_Path(TokenStream& lex, ::AST::HigherRankedBounds hrbs, bool allow_trait_list) {
    Token tok;

    auto ps = lex.start_span();

    auto path = Parse_Path(lex, PATH_GENERIC_TYPE);
    if (lex.lookahead(0) == TOK_EXCLAM) {
        GET_CHECK_TOK(tok, lex, TOK_EXCLAM);
        return TypeRef(TypeRef::TagMacro(), Parse_MacroInvocation(ps, path, lex));
    } else if (hrbs.empty() && !allow_trait_list) {
        return TypeRef(TypeRef::TagPath(), lex.end_span(ps), mv$(path));
    } else {
        ::std::vector<Type_TraitPath> traits;
        ::std::vector<AST::LifetimeRef> lifetimes;

        traits.push_back(Type_TraitPath{mv$(hrbs), mv$(path)});

        if (allow_trait_list) {
            while (lex.getTokenIf(TOK_PLUS)) {
                if (lex.getTokenIf(TOK_LIFETIME, tok)) {
                    lifetimes.push_back(AST::LifetimeRef(/*lex.point_span(),*/ tok.ident()));
                } else {
                    if (lex.lookahead(0) == TOK_RWORD_FOR) {
                        hrbs = Parse_HRB(lex);
                    }
                    traits.push_back({mv$(hrbs), Parse_Path(lex, PATH_GENERIC_TYPE)});
                }
            }
        }

        if (!traits[0].hrbs.empty() || traits.size() > 1 || lifetimes.size() > 0) {
            if (lifetimes.empty()) {
                lifetimes.push_back(AST::LifetimeRef());
            }
            return TypeRef(lex.end_span(ps), mv$(traits), mv$(lifetimes));
        } else {
            return TypeRef(TypeRef::TagPath(), lex.end_span(ps), mv$(*traits.at(0).path));
        }
    }
}

TypeRef Parse_Type_TraitObject(TokenStream& lex, ::AST::HigherRankedBounds hrbs) {
    Token tok;
    auto ps = lex.start_span();

    ::std::vector<Type_TraitPath> traits;
    ::std::vector<AST::LifetimeRef> lifetimes;

    for (;;) {
        bool is_first = traits.empty() && lifetimes.empty();
        if (LOOK_AHEAD(lex) == TOK_LIFETIME) {
            GET_TOK(tok, lex);

            if (is_first && !hrbs.empty()) {
                // TODO: Error
            }

            lifetimes.push_back(AST::LifetimeRef(/*lex.point_span(),*/ tok.ident()));
        } else {
            if (lex.getTokenIf(TOK_RWORD_FOR)) {
                hrbs = Parse_HRB(lex);
            } else {
            }

            bool is_paren = lex.getTokenIf(TOK_PAREN_OPEN);

            traits.push_back({mv$(hrbs), Parse_Path(lex, PATH_GENERIC_TYPE)});

            if (is_paren) {
                GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
            }
        }

        if (!lex.getTokenIf(TOK_PLUS)) {
            break;
        }
    }

    if (lifetimes.empty()) {
        lifetimes.push_back(AST::LifetimeRef());
    }
    return TypeRef(lex.end_span(ps), mv$(traits), mv$(lifetimes));
}

TypeRef Parse_Type_ErasedType(TokenStream& lex, bool allow_trait_list) {
    Token tok;

    auto ps = lex.start_span();
    Type_ErasedType rv_data;
    rv_data.is_edition_2024_or_later = lex.edition_after(AST::Edition::Rust2024);
    do {
        if (lex.getTokenIf(TOK_LIFETIME, tok)) {
            rv_data.lifetimes.push_back(AST::LifetimeRef(/*lex.point_span(),*/ tok.ident()));
        } else if (lex.getTokenIf(TOK_QMARK)) {
            AST::HigherRankedBounds hrbs = Parse_HRB_Opt(lex);
            rv_data.maybe_traits.push_back({mv$(hrbs), Parse_Path(lex, PATH_GENERIC_TYPE)});
        } else if (lex.getTokenIf(TOK_PAREN_OPEN)) {
            AST::HigherRankedBounds hrbs = Parse_HRB_Opt(lex);
            rv_data.traits.push_back({mv$(hrbs), Parse_Path(lex, PATH_GENERIC_TYPE)});
            lex.getTokenCheck(TOK_PAREN_CLOSE);
        } else if (lex.getTokenIf(TOK_RWORD_USE)) {
            lex.getTokenCheck(TOK_LT);
            if (rv_data.use) {
                ERROR(lex.point_span(), E0000, "Multiple `use` seen in erased type");
            }
            rv_data.use.reset(new ::AST::PathParams(Parse_Path_GenericList(lex)));
        } else {
            if (lex.getTokenIf(TOK_TILDE)) {
                GET_CHECK_TOK(tok, lex, TOK_RWORD_CONST);
            } else if (lex.getTokenIf(TOK_RWORD_CONST)) {
            }
            AST::HigherRankedBounds hrbs = Parse_HRB_Opt(lex);
            rv_data.traits.push_back({mv$(hrbs), Parse_Path(lex, PATH_GENERIC_TYPE)});
        }
    } while (lex.getTokenIf(TOK_PLUS));

    return TypeRef(lex.end_span(ps), box$(rv_data));
}
