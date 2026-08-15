#include "parse_common.h"
#include "parse_common.h"

#include "path.h"
#include "ast_ast.h"
#include "hir_hir.h" // ABI_RUST - TODO: Move elsewhere?
#include "ast_expr.h"
#include "settings.h"
#include "ast_crate.h"
#include "ast_types.h"
#include "parse_lex.h"  // New file lexer
#include "expand_cfg.h" // check_cfg - for `mod nonexistant;`
#include "wire_board.h"
#include "parse_tokentree.h"
#include "parse_parseerror.h"
#include "macro_rules_macro_rules.h"
#include "parse_interpolated_fragment.h"

#include <std/mem/obj_pool.h>

#include <cassert>
#include <fstream> // Used by directory path
#include <iostream>

// TODO: Use a ProtoSpan instead of a point span?
static inline ASTExprNodeP mkExprnodep(const TokenStream& lex, ASTExprNode* en) {
    en->setSpan(lex.pointSpan());
    return ASTExprNodeP(en);
}

#define NEWNODE(type, ...) mkExprnodep(lex, new type(__VA_ARGS__))

ASTExprNodeP ParseExprBlockNode(TokenStream& lex, ASTExprNodeBlock::Type ty, Ident label = Ident(""));
ASTExprNodeP ParseExprBlockLineStmt(TokenStream& lex, bool& hasSemicolon);
//ASTExprNodeP Parse_Stmt(TokenStream& lex);   // common.h
ASTExprNodeP ParseStmtLet(TokenStream& lex, bool isSuper = false);
ASTExprNodeP ParseExpr0(TokenStream& lex);
ASTExprNodeP ParseExpr1e(TokenStream& lex); // Boolean OR
ASTExprNodeP ParseExpr3(TokenStream& lex);
ASTExprNodeP ParseIfStmt(TokenStream& lex);
ASTExprNodeP ParseWhileStmt(TokenStream& lex, Ident lifetime);
ASTExprNodeP ParseForStmt(TokenStream& lex, Ident lifetime);
ASTExprNodeP ParseExprMatch(TokenStream& lex);
ASTExprNodeP ParseExpr1(TokenStream& lex);
ASTExprNodeP ParseExprFC(TokenStream& lex);
ASTExprNodeP ParseExprMacro(TokenStream& lex, ASTPath tok);
ASTFunction ParseDelegationFunction(TokenStream& lex, RcString& itemName);
::std::vector<::std::pair<RcString, ASTFunction>> SplitDelegationFunction(const ASTFunction& fcn);

ASTExpr ParseExpr(TokenStream& lex) {
    return ASTExpr(ParseExpr0(lex));
}

ASTExpr ParseExprBlock(TokenStream& lex) {
    return ASTExpr(ParseExprBlockNode(lex));
}

ASTExprNodeP ParseExprBlockNode(TokenStream& lex) {
    return ParseExprBlockNode(lex, ASTExprNodeBlock::Type::Bare, RcString());
}

ASTExprNodeP ParseExprBlockNode(TokenStream& lex, ASTExprNodeBlock::Type ty /*=Bare*/, Ident label /*=RcString()*/) {
    TRACE_FUNCTION;
    CLEAR_PARSE_FLAGS_EXPR(lex);
    Token tok;

    ::std::vector<ASTExprNodeBlock::Line> lines;
    ASTAttributeList attrs;

    auto origModule = lex.parseState().module;
    ::std::shared_ptr<ASTModule> localMod;

    if (LOOK_AHEAD(lex) == TOK_INTERPOLATED_BLOCK) {
        GET_TOK(tok, lex);
        return tok.takeFragNode();
    }

    GET_CHECK_TOK(tok, lex, TOK_BRACE_OPEN);

    while (LOOK_AHEAD(lex) != TOK_BRACE_CLOSE) {
        ParseParentAttrs(lex, attrs);
        if (LOOK_AHEAD(lex) == TOK_BRACE_CLOSE) {
            break;
        }

        bool addSilenceIfEnd = false;
        // `add_silence_if_end` indicates that the statement had a semicolon.
        auto rv = ParseExprBlockLineWithItems(lex, localMod, addSilenceIfEnd);
        if (rv) {
            // Set to TRUE if there was no semicolon after a statement
            lines.push_back({addSilenceIfEnd, mv$(rv)});
        } else {
            assert(!addSilenceIfEnd);
        }
    }
    GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);

    if (lex.parseState().module != origModule) {
        DEBUG("Restore module from " << lex.parseState().module->path() << " to " << origModule->path());
        lex.parseState().module = origModule;
    }
    auto* rvBlk = new ASTExprNodeBlock(ty, mv$(lines), mv$(localMod));
    rvBlk->label = label;
    auto rv = ASTExprNodeP(rvBlk);
    rv->setAttrs(mv$(attrs));
    return rv;
}

/// Parse a single line in a block, handling items added to the local module
///
/// - If an item was parsed, this returns an empty ASTExprNodeP
ASTExprNodeP ParseExprBlockLineWithItems(TokenStream& lex, ::std::shared_ptr<ASTModule>& localMod, bool& addSilenceIfEnd) {
    Token tok;

    auto itemAttrs = ParseItemAttrs(lex);
    GET_TOK(tok, lex);

    // An item statement remains an opaque `stmt` fragment while it is
    // forwarded through macro matchers.  Materialise the contained item only
    // when the fragment is parsed in statement position.
    if (tok.type() == TOK_INTERPOLATED_STMT_ITEM) {
        if (!localMod) {
            localMod = lex.parseState().getCurrentMod().addAnon();
            DEBUG("Set module from " << lex.parseState().module->path() << " to " << localMod->path());
            lex.parseState().module = localMod.get();
        }
        auto item = tok.takeFragStmtItem();
        for (auto& attr : itemAttrs.mItems) {
            item.attrs.mItems.push_back(mv$(attr));
        }
        localMod->addItem(mv$(item));
        return ASTExprNodeP();
    }

    // `union Ident` - contextual keyword
    if (tok.type() == TOK_IDENT && tok.ident().name == "union" && lex.lookahead(0) == TOK_IDENT) {
        PUTBACK(tok, lex);
        if (!localMod) {
            localMod = lex.parseState().getCurrentMod().addAnon();
            DEBUG("Set module from " << lex.parseState().module->path() << " to " << localMod->path());
            lex.parseState().module = localMod.get();
        }
        ParseModItem(lex, *localMod, mv$(itemAttrs));
        return ASTExprNodeP();
    }

    if (tok.type() == TOK_IDENT && tok.ident().name == "reuse"
        && (lex.lookahead(0) == TOK_IDENT || lex.lookahead(0) == TOK_RWORD_SELF
            || lex.lookahead(0) == TOK_RWORD_SUPER || lex.lookahead(0) == TOK_RWORD_CRATE
            || lex.lookahead(0) == TOK_LT)) {
        PUTBACK(tok, lex);
        if (!localMod) {
            localMod = lex.parseState().getCurrentMod().addAnon();
            DEBUG("Set module from " << lex.parseState().module->path() << " to " << localMod->path());
            lex.parseState().module = localMod.get();
        }
        ParseModItem(lex, *localMod, mv$(itemAttrs));
        return ASTExprNodeP();
    }

    if (tok.type() == TOK_IDENT && tok.ident().name == "macro_rules" && lex.lookahead(0) == TOK_EXCLAM) {
        // Special case - create a local module if macro_rules! is seen
        // - Allows correct scoping of defined macros
        if (!localMod) {
            localMod = lex.parseState().getCurrentMod().addAnon();
            DEBUG("Set module from " << lex.parseState().module->path() << " to " << localMod->path());
            lex.parseState().module = localMod.get();
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
        case TOK_RWORD_USE:
            if (lex.lookahead(0) == TOK_PIPE || lex.lookahead(0) == TOK_DOUBLE_PIPE) {
                break;
            }
        case TOK_RWORD_TYPE:
        case TOK_RWORD_EXTERN:
        case TOK_RWORD_STATIC:
            if (lex.lookahead(0) == TOK_PIPE || lex.lookahead(0) == TOK_DOUBLE_PIPE
                || ((lex.lookahead(0) == TOK_RWORD_MOVE || lex.lookahead(0) == TOK_RWORD_USE)
                    && (lex.lookahead(1) == TOK_PIPE || lex.lookahead(1) == TOK_DOUBLE_PIPE))) {
                break;
            }
        case TOK_RWORD_STRUCT:
        case TOK_RWORD_MACRO:
        case TOK_RWORD_ENUM:
        case TOK_RWORD_TRAIT:
        case TOK_RWORD_IMPL:
        case TOK_RWORD_FN:
        case TOK_RWORD_MOD:
            PUTBACK(tok, lex);
            if (!localMod) {
                localMod = lex.parseState().getCurrentMod().addAnon();
                DEBUG("Set module from " << lex.parseState().module->path() << " to " << localMod->path());
                lex.parseState().module = localMod.get();
            }
            ParseModItem(lex, *localMod, mv$(itemAttrs));
            return ASTExprNodeP();
        case TOK_RWORD_ASYNC:
            if (lex.lookahead(0) == TOK_RWORD_FN || (lex.lookahead(0) == TOK_RWORD_UNSAFE && lex.lookahead(1) == TOK_RWORD_FN)) {
                PUTBACK(tok, lex);
                if (!localMod) {
                    localMod = lex.parseState().getCurrentMod().addAnon();
                    DEBUG("Set module from " << lex.parseState().module->path() << " to " << localMod->path());
                    lex.parseState().module = localMod.get();
                }
                ParseModItem(lex, *localMod, mv$(itemAttrs));
                return ASTExprNodeP();
            }
            break;
        // 'const' - Check if the next token isn't a `{`, if so it's an item. Otherwise, fall through
        case TOK_RWORD_CONST:
            if (LOOK_AHEAD(lex) != TOK_BRACE_OPEN) {
                PUTBACK(tok, lex);
                if (!localMod) {
                    localMod = lex.parseState().getCurrentMod().addAnon();
                    DEBUG("Set module from " << lex.parseState().module->path() << " to " << localMod->path());
                    lex.parseState().module = localMod.get();
                }
                ParseModItem(lex, *localMod, mv$(itemAttrs));
                return ASTExprNodeP();
            }
            break;
        // 'unsafe' - Check if the next token isn't a `{`, if so it's an item. Otherwise, fall through
        case TOK_RWORD_UNSAFE:
            if (LOOK_AHEAD(lex) != TOK_BRACE_OPEN) {
                PUTBACK(tok, lex);
                if (!localMod) {
                    localMod = lex.parseState().getCurrentMod().addAnon();
                    DEBUG("Set module from " << lex.parseState().module->path() << " to " << localMod->path());
                    lex.parseState().module = localMod.get();
                }
                ParseModItem(lex, *localMod, mv$(itemAttrs));
                return ASTExprNodeP();
            }
            // fall
        default:
            break;
    }
    PUTBACK(tok, lex);
    auto rv = ParseExprBlockLine(lex, &addSilenceIfEnd);
    if (rv) {
        rv->setAttrs(mv$(itemAttrs));
    } else if (itemAttrs.mItems.size() > 0) {
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
ASTExprNodeP ParseExprBlockLine(TokenStream& lex, bool* addSilence) {
    TRACE_FUNCTION;
    Token tok;
    ASTExprNodeP ret;
    bool addSilenceIgnored = false;
    if (!addSilence) {
        addSilence = &addSilenceIgnored;
    }

    if (GET_TOK(tok, lex) == TOK_LIFETIME) {
        // Lifetimes can only precede loops... and blocks?
        auto lifetime = tok.ident();
        GET_CHECK_TOK(tok, lex, TOK_COLON);

        switch (GET_TOK(tok, lex)) {
            case TOK_RWORD_LOOP:
                return NEWNODE(ASTExprNodeLoop, lifetime, ParseExprBlockNode(lex));
            case TOK_RWORD_WHILE:
                return ParseWhileStmt(lex, lifetime);
            case TOK_RWORD_FOR:
                return ParseForStmt(lex, lifetime);
            // NOTE: 1.39's libsyntax uses labelled block
            case TOK_BRACE_OPEN:
                PUTBACK(tok, lex);
                ret = ParseExprBlockNode(lex, /*is_unsafe*/ ASTExprNodeBlock::Type::Bare, lifetime);
                return ret;
            case TOK_RWORD_UNSAFE:
                ret = ParseExprBlockNode(lex, /*is_unsafe*/ ASTExprNodeBlock::Type::Unsafe, lifetime);
                return ret;
            case TOK_RWORD_CONST:
                ret = ParseExprBlockNode(lex, /*is_unsafe*/ ASTExprNodeBlock::Type::Const, lifetime);
                return ret;
                // TODO: Can these have labels?
                //case TOK_RWORD_IF:
                //case TOK_RWORD_MATCH:

            default:
                throw ParseErrorUnexpected(lex, tok);
        }
    } else {
        if (tok.type() == TOK_RWORD_SUPER && lex.lookahead(0) == TOK_RWORD_LET) {
            GET_CHECK_TOK(tok, lex, TOK_RWORD_LET);
            ret = ParseStmtLet(lex, true);
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
            return ret;
        }

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
                    auto p = ParsePath(lex, PATH_GENERIC_EXPR);
                    if (lex.lookahead(0) == TOK_EXCLAM && lex.lookahead(1) == TOK_BRACE_OPEN) {
                        GET_CHECK_TOK(tok, lex, TOK_EXCLAM);
                        auto rv = ParseExprMacro(lex, std::move(p));
                        // If the block is followed by `.` or `?`, it's actually an expression!
                        if (lex.lookahead(0) == TOK_DOT || lex.lookahead(0) == TOK_QMARK) {
                            lex.putback(Token(Token::TagTakeIP(), InterpolatedFragment(InterpolatedFragment::EXPR, rv.release())));
                            return ParseExprBlockLineStmt(lex, *addSilence);
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
                return tok.takeFragNode();
            case TOK_SEMICOLON:
                // Return a NULL expression, nothing here.
                return nullptr;

            // let binding
            case TOK_RWORD_LET:
                ret = ParseStmtLet(lex);
                GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
                return ret;

            // Blocks that don't need semicolons
            // NOTE: If these are followed by a small set of tokens (`.` and `?`) then they are actually the start of an expression
            // HACK: Parse here, but if the next token is one of the set store in a TOK_INTERPOLATED_EXPR and invoke the statement parser
            case TOK_RWORD_LOOP: {
                ret = NEWNODE(ASTExprNodeLoop, "", ParseExprBlockNode(lex));
                break;
            }
            case TOK_RWORD_WHILE:
                ret = ParseWhileStmt(lex, Ident(""));
                break;
            case TOK_RWORD_FOR:
                ret = ParseForStmt(lex, Ident(""));
                break;
            case TOK_RWORD_IF:
                ret = ParseIfStmt(lex);
                break;
            case TOK_RWORD_MATCH:
                ret = ParseExprMatch(lex);
                break;
            case TOK_RWORD_UNSAFE:
                ret = ParseExprBlockNode(lex, ASTExprNodeBlock::Type::Unsafe);
                break;
            case TOK_RWORD_CONST:
                ret = ParseExprBlockNode(lex, ASTExprNodeBlock::Type::Const);
                break;
            case TOK_BRACE_OPEN:
                PUTBACK(tok, lex);
                ret = ParseExprBlockNode(lex);
                break;

            // Flow control
            case TOK_RWORD_DO:
                // `do yeet`
            case TOK_RWORD_BECOME:
            case TOK_RWORD_RETURN:
            case TOK_RWORD_YIELD:
            case TOK_RWORD_CONTINUE:
            case TOK_RWORD_BREAK: {
                PUTBACK(tok, lex);
                auto ret = ParseStmt(lex);
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

            default:
                PUTBACK(tok, lex);
                return ParseExprBlockLineStmt(lex, *addSilence);
        }

        // If the block is followed by `.` or `?`, it's actually an expression!
        if (lex.lookahead(0) == TOK_DOT || lex.lookahead(0) == TOK_QMARK) {
            lex.putback(Token(Token::TagTakeIP(), InterpolatedFragment(InterpolatedFragment::EXPR, ret.release())));
            return ParseExprBlockLineStmt(lex, *addSilence);
        }

        if (LOOK_AHEAD(lex) == TOK_SEMICOLON) {
            GET_TOK(tok, lex);
            *addSilence = true;
        }

        return ret;
    }
}

ASTExprNodeP ParseExprBlockLineStmt(TokenStream& lex, bool& hasSemicolon) {
    Token tok;

    bool isParen = lex.lookahead(0) == TOK_PAREN_OPEN;

    auto ret = ParseStmt(lex);

    // If `ret` is a braced macro call, don't require the semicolon (to remove the hackiness above)
    // - Don't trigger this when parens are present
    if (const auto* mac = cast<ASTExprNodeMacro>(&*ret)) {
        if (!isParen && mac->isBraced) {
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
        hasSemicolon = true;
    }
    return ret;
}

std::vector<ASTIfLetCondition> ParseIfLetChain(TokenStream& lex) {
    Token tok;
    std::vector<ASTIfLetCondition> conditions;
    bool hadPat = false;
    do {
        if (lex.getTokenIf(TOK_RWORD_LET)) {
            lex.getTokenIf(TOK_PIPE);
            auto pat = ParsePattern(lex, AllowOrPattern::Yes);
            GET_CHECK_TOK(tok, lex, TOK_EQUAL);
            ASTExprNodeP val;
            {
                SET_PARSE_FLAG(lex, disallowStructLiteral);
                val = ParseExpr3(lex); // This is just after `||` and `&&`
            }
            conditions.push_back(ASTIfLetCondition{box$(pat), std::move(val)});
            hadPat = true;
        } else {
            ASTExprNodeP val;
            {
                SET_PARSE_FLAG(lex, disallowStructLiteral);
                val = ParseExpr3(lex); // This is just after `||` and `&&`
            }

            // Chain boolean expressions to simplify downstream representation
            if (conditions.size() > 0 && !conditions.back().optPat) {
                conditions.back().value = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::BOOLAND, ::std::move(conditions.back().value), ::std::move(val));
            } else {
                conditions.push_back(ASTIfLetCondition{std::unique_ptr<ASTPattern>(), std::move(val)});
            }
        }
    } while (lex.getTokenIf(TOK_DOUBLE_AMP));

    if (lex.lookahead(0) == TOK_DOUBLE_PIPE) {
        if (hadPat) {
            TODO(lex.pointSpan(), "lazy boolean or in let chains not yet implemented (not yet valid rust, at 1.75)");
        } else {
            // Fall back to parsing as a standard expression
            auto prev = ::std::move(conditions[0].value);
            for (size_t i = 1; i < conditions.size(); i++) {
                prev = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::BOOLAND, ::std::move(prev), ::std::move(conditions[i].value));
            }
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_PIPE);
            SET_PARSE_FLAG(lex, disallowStructLiteral);
            auto n = ParseExpr1e(lex); // Boolean or
            auto rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::BOOLOR, ::std::move(prev), ::std::move(n));

            conditions.clear();
            conditions.push_back(ASTIfLetCondition{std::unique_ptr<ASTPattern>(), std::move(rv)});
        }
    }

    return conditions;
}

/// While loop (either as a statement, or as part of an expression)
ASTExprNodeP ParseWhileStmt(TokenStream& lex, Ident lifetime) {
    auto conditions = ParseIfLetChain(lex);
    return NEWNODE(ASTExprNodeWhile, lifetime, ::std::move(conditions), ParseExprBlockNode(lex));
}

/// For loop (either as a statement, or as part of an expression)
ASTExprNodeP ParseForStmt(TokenStream& lex, Ident lifetime) {
    CLEAR_PARSE_FLAGS_EXPR(lex);
    Token tok;

    // Irrefutable pattern
    auto pat = ParsePattern(lex, AllowOrPattern::Yes);
    GET_CHECK_TOK(tok, lex, TOK_RWORD_IN);
    ASTExprNodeP val;
    {
        SET_PARSE_FLAG(lex, disallowStructLiteral);
        val = ParseExpr0(lex);
    }
    return NEWNODE(ASTExprNodeFor, lifetime, ::std::move(pat), ::std::move(val), ParseExprBlockNode(lex));
}

/// Parse an 'if' statement
// Note: TOK_RWORD_IF has already been eaten
ASTExprNodeP ParseIfStmt(TokenStream& lex) {
    TRACE_FUNCTION;

    Token tok;
    std::vector<ASTExprNodeIf::Arm> arms;
    ASTExprNodeP elseBlock;
    do {
        std::vector<ASTIfLetCondition> conditions;

        {
            SET_PARSE_FLAG(lex, disallowStructLiteral);
            conditions = ParseIfLetChain(lex);
        }

        // Contents
        ASTExprNodeP code = ParseExprBlockNode(lex);

        arms.push_back(ASTExprNodeIf::Arm{std::move(conditions), std::move(code)});

        // Handle else:
        if (!lex.getTokenIf(TOK_RWORD_ELSE)) {
            // No `else`, leave `else_block` as `nullptr`
            break;
        }
        // Recurse for 'else if'
        if (!lex.getTokenIf(TOK_RWORD_IF)) {
            elseBlock = ParseExprBlockNode(lex);
            break;
        }
        // Keep looping
    } while (true);

    return NEWNODE(ASTExprNodeIf, ::std::move(arms), ::std::move(elseBlock));
}

/// "match" block
ASTExprNodeP ParseExprMatch(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;
    ASTAttributeList nodeAttrs;

    CLEAR_PARSE_FLAGS_EXPR(lex);
    // 1. Get expression
    ASTExprNodeP switchVal;
    {
        SET_PARSE_FLAG(lex, disallowStructLiteral);
        switchVal = ParseExpr1(lex);
    }
    GET_CHECK_TOK(tok, lex, TOK_BRACE_OPEN);

    ::std::vector<ASTExprNodeMatchArm> arms;
    do {
        if (lex.getTokenIf(TOK_BRACE_CLOSE, tok)) {
            break;
        }
        ASTExprNodeMatchArm arm;

        ParseParentAttrs(lex, nodeAttrs);
        arm.mAttrs = ParseItemAttrs(lex);

        // Match-arm grammar permits an optional leading `|` before the first pattern.
        lex.getTokenIf(TOK_PIPE);

        do {
            // Refutable pattern
            arm.patterns.push_back(ParsePattern(lex, AllowOrPattern::No));
        } while (GET_TOK(tok, lex) == TOK_PIPE);

        if (tok.type() == TOK_RWORD_IF) {
            arm.guard = ParseIfLetChain(lex);
            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_FATARROW);

        arm.mCode = ParseStmt(lex);

        arms.push_back(::std::move(arm));

        // Match arms don't need a trailing comma (TODO: Only if braced)
        lex.getTokenIf(TOK_COMMA);
    } while (1);
    CHECK_TOK(tok, TOK_BRACE_CLOSE);

    auto rv = NEWNODE(ASTExprNodeMatch, ::std::move(switchVal), ::std::move(arms));
    rv->setAttrs(std::move(nodeAttrs));
    return rv;
}

/// "do catch" block
ASTExprNodeP ParseExprTry(TokenStream& lex) {
    TRACE_FUNCTION;
    //Token   tok;

    auto inner = ParseExprBlockNode(lex);
    //TODO(lex.point_span(), "do catch");
    return NEWNODE(ASTExprNodeTry, ::std::move(inner));
}

ASTExprNodeP ParseFlowControl(TokenStream& lex, ASTExprNodeFlow::Type type) {
    Token tok;
    Ident lifetime = Ident("");
    // continue/break can specify a target
    if (type == ASTExprNodeFlow::CONTINUE || type == ASTExprNodeFlow::BREAK) {
        if (lex.lookahead(0) == TOK_LIFETIME) {
            GET_TOK(tok, lex);
            lifetime = tok.ident();
        }
    }
    // Return value
    // TODO: Should this prevent `continue value;`?
    ASTExprNodeP val;
    if (type == ASTExprNodeFlow::BREAK
        && LOOK_AHEAD(lex) == TOK_BRACE_OPEN
        && CHECK_PARSE_FLAG(lex, disallowStructLiteral)) {
        return NEWNODE(ASTExprNodeFlow, type, std::move(lifetime), std::move(val));
    }
    switch (LOOK_AHEAD(lex)) {
        case TOK_EOF:
        case TOK_SEMICOLON:
        case TOK_COMMA:
        case TOK_BRACE_CLOSE:
        case TOK_PAREN_CLOSE:
        case TOK_SQUARE_CLOSE:
            break;
        default:
            val = ParseExpr0(lex);
            break;
    }
    return NEWNODE(ASTExprNodeFlow, type, std::move(lifetime), ::std::move(val));
}

/// Parses the 'stmt' fragment specifier
/// - Flow control
/// - Expressions
ASTExprNodeP ParseStmt(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    switch (GET_TOK(tok, lex)) {
        case TOK_INTERPOLATED_STMT:
            return tok.takeFragNode();
        // Duplicated here for the :stmt pattern fragment.
        case TOK_RWORD_LET:
            return ParseStmtLet(lex);
        case TOK_RWORD_SUPER:
            if (lex.getTokenIf(TOK_RWORD_LET)) {
                return ParseStmtLet(lex, true);
            }
            PUTBACK(tok, lex);
            return ParseExpr0(lex);
        case TOK_RWORD_YIELD:
            return ParseFlowControl(lex, ASTExprNodeFlow::YIELD);
        case TOK_RWORD_CONTINUE:
            return ParseFlowControl(lex, ASTExprNodeFlow::CONTINUE);
        case TOK_RWORD_BREAK:
            return ParseFlowControl(lex, ASTExprNodeFlow::BREAK);
        case TOK_RWORD_RETURN:
            return ParseFlowControl(lex, ASTExprNodeFlow::RETURN);
        case TOK_RWORD_BECOME:
            return ParseFlowControl(lex, ASTExprNodeFlow::TAILCALL);
        case TOK_BRACE_OPEN:
            PUTBACK(tok, lex);
            return ParseExprBlockNode(lex);
        case TOK_RWORD_IF:
        case TOK_RWORD_WHILE:
        case TOK_RWORD_FOR:
        case TOK_RWORD_LOOP:
        case TOK_RWORD_MATCH: {
            PUTBACK(tok, lex);
            SET_PARSE_FLAG(lex, disallowCallOrIndex);
            return ParseExprFC(lex);
        }
        //case TOK_RWORD_DO:
        //    }
        default:
            PUTBACK(tok, lex);
            return ParseExpr0(lex);
    }
}

ASTExprNodeP ParseStmtLet(TokenStream& lex, bool isSuper) {
    Token tok;
    ASTPattern pat = ParsePattern(lex, AllowOrPattern::Yes); // irrefutable
    ASTType* type = mkType(lex.typePool(), lex.pointSpan());
    if (lex.getTokenIf(TOK_COLON)) {
        type = ParseType(lex);
    }
    ASTExprNodeP val;
    ASTExprNodeP elseArm;
    if (lex.getTokenIf(TOK_EQUAL)) {
        val = ParseExpr0(lex);
        if (lex.getTokenIf(TOK_RWORD_ELSE)) {
            elseArm = ParseExprBlockNode(lex);
        }
    }
    return NEWNODE(ASTExprNodeLetBinding, ::std::move(pat), mv$(type), ::std::move(val), ::std::move(elseArm), isSuper);
}

::std::vector<ASTExprNodeP> ParseParenList(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    CLEAR_PARSE_FLAGS_EXPR(lex);

    ::std::vector<ASTExprNodeP> rv;
    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    if (!lex.getTokenIf(TOK_PAREN_CLOSE)) {
        do {
            if (lex.getTokenIf(TOK_PAREN_CLOSE, tok)) {
                break;
            }
            rv.push_back(ParseExpr0(lex));
        } while (GET_TOK(tok, lex) == TOK_COMMA);
        CHECK_TOK(tok, TOK_PAREN_CLOSE);
    }
    return rv;
}

// 0: Assign
ASTExprNodeP ParseExpr0(TokenStream& lex) {
    //TRACE_FUNCTION;
    Token tok;

    CLEAR_PARSE_FLAG(lex, disallowCallOrIndex);

    auto exprAttrs = ParseItemAttrs(lex);

    ASTExprNodeP rv = ParseExpr1(lex);
    auto op = ASTExprNodeAssign::NONE;
    switch (GET_TOK(tok, lex)) {
        case TOK_PLUS_EQUAL:
            op = ASTExprNodeAssign::ADD;
            if (0) {
                case TOK_DASH_EQUAL:
                    op = ASTExprNodeAssign::SUB;
            }
            if (0) {
                case TOK_STAR_EQUAL:
                    op = ASTExprNodeAssign::MUL;
            }
            if (0) {
                case TOK_SLASH_EQUAL:
                    op = ASTExprNodeAssign::DIV;
            }
            if (0) {
                case TOK_PERCENT_EQUAL:
                    op = ASTExprNodeAssign::MOD;
            }
            if (0) {
                case TOK_AMP_EQUAL:
                    op = ASTExprNodeAssign::AND;
            }
            if (0) {
                case TOK_PIPE_EQUAL:
                    op = ASTExprNodeAssign::OR;
            }
            if (0) {
                case TOK_CARET_EQUAL:
                    op = ASTExprNodeAssign::XOR;
            }
            if (0) {
                case TOK_DOUBLE_GT_EQUAL:
                    op = ASTExprNodeAssign::SHR;
            }
            if (0) {
                case TOK_DOUBLE_LT_EQUAL:
                    op = ASTExprNodeAssign::SHL;
            }
            if (0) {
                case TOK_EQUAL:
                    op = ASTExprNodeAssign::NONE;
            }
            rv = NEWNODE(ASTExprNodeAssign, op, ::std::move(rv), ParseExpr0(lex));
            rv->setAttrs(mv$(exprAttrs));
            return rv;

        default:
            PUTBACK(tok, lex);
            rv->setAttrs(mv$(exprAttrs));
            return rv;
    }
}

#define LEFTASSOC(cur, _next, cases)                 \
    ASTExprNodeP _next(TokenStream& lex);            \
    ASTExprNodeP cur(TokenStream& lex) {             \
        ASTExprNodeP (*next)(TokenStream&) = _next;  \
        ASTExprNodeP rv = next(lex);                 \
        auto parseNext = [&lex, next]() {            \
            return ParseIsRangeSeparator(LOOK_AHEAD(lex)) ? ParseExpr1a(lex) : next(lex); \
        };                                           \
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

bool ParseIsRangeSeparator(eTokenType tokType) {
    return tokType == TOK_DOUBLE_DOT || tokType == TOK_DOUBLE_DOT_EQUAL || tokType == TOK_TRIPLE_DOT;
}

bool ParseIsTokValue(eTokenType tokType) {
    switch (tokType) {
        case TOK_DOUBLE_DOT:
        case TOK_DOUBLE_DOT_EQUAL:
        case TOK_TRIPLE_DOT:
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

ASTExprNodeP ParseExpr1a(TokenStream& lex);

ASTExprNodeP ParseExpr1(TokenStream& lex) {
    Token tok;
    ASTExprNodeP (*next)(TokenStream&) = ParseExpr1a;

    auto dest = next(lex);
    if (lex.lookahead(0) == TOK_THINARROW_LEFT) {
        GET_TOK(tok, lex);
        auto val = ParseExpr1(lex);
        return NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::PLACE_IN, mv$(dest), mv$(val));
    } else {
        return dest;
    }
}

ASTExprNodeP ParseExpr1b(TokenStream& lex);

// Very evil handling for '..'
ASTExprNodeP ParseExpr1a(TokenStream& lex) {
    Token tok;
    ASTExprNodeP (*next)(TokenStream&) = ParseExpr1b;
    ASTExprNodeP left, right;

    // Inclusive range to a value
    if (GET_TOK(tok, lex) == TOK_TRIPLE_DOT || tok.type() == TOK_DOUBLE_DOT_EQUAL) {
        right = ParseIsRangeSeparator(LOOK_AHEAD(lex)) ? ParseExpr1a(lex) : next(lex);
        return NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::RANGE_INC, nullptr, mv$(right));
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
    if (ParseIsTokValue(LOOK_AHEAD(lex))) {
        right = ParseIsRangeSeparator(LOOK_AHEAD(lex)) ? ParseExpr1a(lex) : next(lex);
    } else {
        // Otherwise, leave `right` as nullptr
    }

    return NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::RANGE, ::std::move(left), ::std::move(right));
}
LEFTASSOC(ParseExpr1b, ParseExpr1e, case TOK_TRIPLE_DOT : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::RANGE_INC, mv$(rv), parseNext()); break; case TOK_DOUBLE_DOT_EQUAL : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::RANGE_INC, mv$(rv), parseNext()); break;)
// 1: Bool OR
LEFTASSOC(ParseExpr1e, ParseExpr2, case TOK_DOUBLE_PIPE : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::BOOLOR, ::std::move(rv), parseNext()); break;)
// 2: Bool AND
LEFTASSOC(ParseExpr2, ParseExpr3, case TOK_DOUBLE_AMP : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::BOOLAND, ::std::move(rv), parseNext()); break;)
// 3: (In)Equality
LEFTASSOC(ParseExpr3, ParseExpr4, case TOK_DOUBLE_EQUAL : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::CMPEQU, ::std::move(rv), parseNext()); break; case TOK_EXCLAM_EQUAL : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::CMPNEQU, ::std::move(rv), parseNext()); break;)
// 4: Comparisons
LEFTASSOC(ParseExpr4, ParseExpr5, case TOK_LT : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::CMPLT, ::std::move(rv), parseNext()); break; case TOK_GT : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::CMPGT, ::std::move(rv), parseNext()); break; case TOK_LTE : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::CMPLTE, ::std::move(rv), parseNext()); break; case TOK_GTE : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::CMPGTE, ::std::move(rv), parseNext()); break;)
// 5: Bit OR
LEFTASSOC(ParseExpr5, ParseExpr6, case TOK_PIPE : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::BITOR, ::std::move(rv), parseNext()); break;)
// 6: Bit XOR
LEFTASSOC(ParseExpr6, ParseExpr7, case TOK_CARET : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::BITXOR, ::std::move(rv), parseNext()); break;)
// 7: Bit AND
LEFTASSOC(ParseExpr7, ParseExpr8, case TOK_AMP : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::BITAND, ::std::move(rv), parseNext()); break;)
// 8: Bit Shifts
LEFTASSOC(ParseExpr8, ParseExpr9, case TOK_DOUBLE_LT : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::SHL, ::std::move(rv), parseNext()); break; case TOK_DOUBLE_GT : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::SHR, ::std::move(rv), parseNext()); break;)
// 9: Add / Subtract
LEFTASSOC(ParseExpr9, ParseExpr10, case TOK_PLUS : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::ADD, ::std::move(rv), parseNext()); break; case TOK_DASH : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::SUB, ::std::move(rv), parseNext()); break;)
// 10: Times / Divide / Modulo
LEFTASSOC(ParseExpr10, ParseExpr11, case TOK_STAR : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::MULTIPLY, ::std::move(rv), parseNext()); break; case TOK_SLASH : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::DIVIDE, ::std::move(rv), parseNext()); break; case TOK_PERCENT : rv = NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::MODULO, ::std::move(rv), parseNext()); break;)
// 11: Cast
LEFTASSOC(ParseExpr11, ParseExpr12, case TOK_RWORD_AS : rv = NEWNODE(ASTExprNodeCast, ::std::move(rv), ParseType(lex, false)); break;)
// 12: Type Ascription
ASTExprNodeP ParseExpr13(TokenStream& lex);

ASTExprNodeP ParseExpr12(TokenStream& lex) {
    Token tok;
    auto rv = ParseExpr13(lex);
    if (lex.getTokenIf(TOK_COLON)) {
        rv = NEWNODE(ASTExprNodeTypeAnnotation, mv$(rv), ParseType(lex));
    }
    return rv;
}

ASTExprNodeP ParseExprUnaryOperand(TokenStream& lex) {
    return ParseIsRangeSeparator(LOOK_AHEAD(lex)) ? ParseExpr1a(lex) : ParseExpr12(lex);
}

// 13: Unaries
ASTExprNodeP ParseExpr13(TokenStream& lex) {
    Token tok;
    switch (GET_TOK(tok, lex)) {
        case TOK_DASH:
            return NEWNODE(ASTExprNodeUniOp, ASTExprNodeUniOp::NEGATE, ParseExprUnaryOperand(lex));
        case TOK_EXCLAM:
            return NEWNODE(ASTExprNodeUniOp, ASTExprNodeUniOp::INVERT, ParseExprUnaryOperand(lex));
        case TOK_STAR:
            return NEWNODE(ASTExprNodeDeref, ParseExprUnaryOperand(lex));
        case TOK_RWORD_BOX:
            return NEWNODE(ASTExprNodeUniOp, ASTExprNodeUniOp::BOX, ParseExprUnaryOperand(lex));
        case TOK_RWORD_IN: {
            ASTExprNodeP dest;
            {
                SET_PARSE_FLAG(lex, disallowStructLiteral);
                dest = ParseExpr1(lex);
            }
            auto val = ParseExprBlockNode(lex);
            return NEWNODE(ASTExprNodeBinOp, ASTExprNodeBinOp::PLACE_IN, mv$(dest), mv$(val));
        }
        case TOK_DOUBLE_AMP:
            // The lexer maximally tokenizes `&&`; unary borrow parsing consumes it as two `&` tokens.
            lex.putback(Token(TOK_AMP));
        case TOK_AMP:
            if (lex.lookahead(0) == TOK_IDENT) {
                GET_TOK(tok, lex);
                if (tok.ident() == "raw") {
                    if (lex.lookahead(0) == TOK_RWORD_MUT) {
                        GET_TOK(tok, lex);
                        return NEWNODE(ASTExprNodeUniOp, ASTExprNodeUniOp::RawBorrowMut, ParseExprUnaryOperand(lex));
                    } else if (lex.lookahead(0) == TOK_RWORD_CONST) {
                        GET_TOK(tok, lex);
                        return NEWNODE(ASTExprNodeUniOp, ASTExprNodeUniOp::RawBorrow, ParseExprUnaryOperand(lex));
                    } else {
                    }
                }
                PUTBACK(tok, lex);
            }
            if (lex.getTokenIf(TOK_RWORD_MUT)) {
                return NEWNODE(ASTExprNodeUniOp, ASTExprNodeUniOp::REFMUT, ParseExprUnaryOperand(lex));
            } else {
                return NEWNODE(ASTExprNodeUniOp, ASTExprNodeUniOp::REF, ParseExprUnaryOperand(lex));
            }
        default:
            PUTBACK(tok, lex);
            return ParseExprFC(lex);
    }
}

ASTExprNodeP ParseExprVal(TokenStream& lex);

ASTExprNodeP ParseExprFC(TokenStream& lex) {
    ASTExprNodeP val = ParseExprVal(lex);
    while (true) {
        Token tok;
        switch (GET_TOK(tok, lex)) {
            // Expression method call
            case TOK_PAREN_OPEN:
                if (CHECK_PARSE_FLAG(lex, disallowCallOrIndex)) {
                    PUTBACK(tok, lex);
                    return val;
                }
                PUTBACK(tok, lex);
                val = NEWNODE(ASTExprNodeCallObject, ::std::move(val), ParseParenList(lex));
                break;
            case TOK_SQUARE_OPEN:
                if (CHECK_PARSE_FLAG(lex, disallowCallOrIndex)) {
                    PUTBACK(tok, lex);
                    return val;
                }
                val = NEWNODE(ASTExprNodeIndex, ::std::move(val), ParseExpr0(lex));
                GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                break;

            case TOK_QMARK:
                val = NEWNODE(ASTExprNodeUniOp, ASTExprNodeUniOp::QMARK, mv$(val));
                break;

            case TOK_DOT:
                // Field access / method call / tuple index
                switch (GET_TOK(tok, lex)) {
                    case TOK_IDENT: {
                        ASTPathNode pn(tok.ident().name, {});
                        switch (GET_TOK(tok, lex)) {
                            case TOK_PAREN_OPEN:
                                PUTBACK(tok, lex);
                                val = NEWNODE(ASTExprNodeCallMethod, ::std::move(val), ::std::move(pn), ParseParenList(lex));
                                break;
                            case TOK_DOUBLE_COLON:
                                if (lex.getTokenIf(TOK_DOUBLE_LT)) {
                                    lex.putback(Token(TOK_LT));
                                } else {
                                    GET_CHECK_TOK(tok, lex, TOK_LT);
                                }
                                pn.args() = ParsePathGenericList(lex);
                                val = NEWNODE(ASTExprNodeCallMethod, ::std::move(val), ::std::move(pn), ParseParenList(lex));
                                break;
                            default:
                                val = NEWNODE(ASTExprNodeField, ::std::move(val), pn.name());
                                PUTBACK(tok, lex);
                                break;
                        }
                        break;
                    }
                    case TOK_INTEGER:
                        val = NEWNODE(ASTExprNodeField, ::std::move(val), RcString::newInterned(FMT(tok.intval())));
                        break;
                    case TOK_FLOAT: {
                        const auto value = tok.floatval();
                        const auto whole = floatValueTrunc(value);
                        const auto fraction = value - whole;
                        auto scale = FloatValue(1.0);
                        FloatValue fractionalIndex;
                        const auto tolerancePerUnit = parseFloatValue("1e-32");
                        bool foundFractionalIndex = false;
                        for (unsigned digits = 0; digits < 20; digits++) {
                            scale = scale * FloatValue(10.0);
                            const auto scaled = fraction * scale;
                            fractionalIndex = floatValueRound(scaled);
                            if (floatValueAbs(scaled - fractionalIndex) <= scale * tolerancePerUnit) {
                                foundFractionalIndex = true;
                                break;
                            }
                        }
                        const auto indexLimit = FloatValue(18446744073709551616.0);
                        if (!foundFractionalIndex || value < FloatValue() || whole >= indexLimit || fractionalIndex >= indexLimit) {
                            throw ParseErrorUnexpected(lex, mv$(tok));
                        }
                        val = NEWNODE(ASTExprNodeField, ::std::move(val), RcString::newInterned(FMT(static_cast<uint64_t>(whole))));
                        val = NEWNODE(ASTExprNodeField, ::std::move(val), RcString::newInterned(FMT(static_cast<uint64_t>(fractionalIndex))));
                        break;
                    }
                    case TOK_RWORD_AWAIT:
                        val = NEWNODE(ASTExprNodeUniOp, ASTExprNodeUniOp::AWait, ::std::move(val));
                        break;
                    case TOK_RWORD_USE:
                        val = NEWNODE(ASTExprNodeUniOp, ASTExprNodeUniOp::USE, ::std::move(val));
                        break;
                    default:
                        throw ParseErrorUnexpected(lex, mv$(tok));
                }
                break;
            default:
                PUTBACK(tok, lex);
                return val;
        }
    }
}

ASTExprNodeP ParseExprValStructLiteral(TokenStream& lex, ASTPath path) {
    TRACE_FUNCTION;
    Token tok;

    // #![feature(relaxed_adts)]
    if (LOOK_AHEAD(lex) == TOK_INTEGER) {
        ::std::map<unsigned int, ASTExprNodeP> nodes;
        while (GET_TOK(tok, lex) == TOK_INTEGER) {
            unsigned int ofs = static_cast<unsigned int>(tok.intval().truncateU64());
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            ASTExprNodeP val = ParseStmt(lex);
            if (!nodes.insert(::std::make_pair(ofs, mv$(val))).second) {
                ERROR(lex.pointSpan(), E0000, "Duplicate index");
            }

            if (GET_TOK(tok, lex) == TOK_BRACE_CLOSE) {
                break;
            }
            CHECK_TOK(tok, TOK_COMMA);
        }
        CHECK_TOK(tok, TOK_BRACE_CLOSE);

        ::std::vector<ASTExprNodeP> items;
        unsigned int i = 0;
        for (auto& p : nodes) {
            if (p.first != i) {
                ERROR(lex.pointSpan(), E0000, "Missing index " << i);
            }
            items.push_back(mv$(p.second));
            i++;
        }

        return NEWNODE(ASTExprNodeCallPath, mv$(path), mv$(items));
    }

    // Braced structure literal
    // - A series of 0 or more pairs of <ident>: <expr>,
    // - '..' <expr>
    ASTExprNodeStructLiteral::tValues items;
    while (GET_TOK(tok, lex) == TOK_IDENT || tok.type() == TOK_HASH) {
        ASTAttributeList attrs; // Note: Parse_ItemAttrs uses lookahead, so can't use it here.
        if (tok.type() == TOK_HASH) {
            PUTBACK(tok, lex);
            attrs = ParseItemAttrs(lex);
            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_IDENT);
        auto h = tok.ident().hygiene;
        auto name = tok.ident().name;

        ASTExprNodeP val;
        if (lex.lookahead(0) != TOK_COLON) {
            val = NEWNODE(ASTExprNodeNamedValue, ASTPath::newRelative(h, {ASTPathNode(name)}));
        } else {
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            val = ParseExpr0(lex);
        }
        items.push_back(ASTExprNodeStructLiteral::Ent{mv$(attrs), mv$(name), mv$(val)});

        if (GET_TOK(tok, lex) == TOK_BRACE_CLOSE) {
            break;
        }
        CHECK_TOK(tok, TOK_COMMA);
    }
    ASTExprNodeP baseVal;
    if (tok.type() == TOK_DOUBLE_DOT) {
        if (lex.getTokenIf(TOK_BRACE_CLOSE)) {
            return NEWNODE(ASTExprNodeStructLiteralPattern, path, ::std::move(items));
        } else {
            // default
            baseVal = ParseExpr0(lex);
        }
        GET_TOK(tok, lex);
    }
    CHECK_TOK(tok, TOK_BRACE_CLOSE);

    return NEWNODE(ASTExprNodeStructLiteral, path, ::std::move(baseVal), ::std::move(items));
}

ASTExprNodeP ParseExprValClosure(TokenStream& lex, bool isAsync = false) {
    TRACE_FUNCTION;
    Token tok;

    GET_TOK(tok, lex);

    // [`static`]
    bool isImmovable = false;
    if (tok == TOK_RWORD_STATIC) {
        GET_TOK(tok, lex);
        isImmovable = true;
    }

    // [`move` | `use`]
    bool isMove = false;
    bool isUse = false;
    if (tok == TOK_RWORD_MOVE) {
        GET_TOK(tok, lex);
        isMove = true;
    } else if (tok == TOK_RWORD_USE) {
        GET_TOK(tok, lex);
        isUse = true;
    }

    ::std::vector<::std::pair<ASTPattern, ASTType*>> args;
    if (tok == TOK_DOUBLE_PIPE) {
        // `||` - Empty argument list
    } else if (tok == TOK_PIPE) {
        // `|...|` - Arguments present
        while (!lex.getTokenIf(TOK_PIPE, tok)) {
            // Irrefutable pattern
            ASTPattern pat = ParsePattern(lex, AllowOrPattern::No);

            ASTType* type = mkType(lex.typePool(), lex.pointSpan());
            if (lex.getTokenIf(TOK_COLON)) {
                type = ParseType(lex);
            }

            args.push_back(::std::make_pair(::std::move(pat), ::std::move(type)));

            if (!lex.getTokenIf(TOK_COMMA)) {
                GET_TOK(tok, lex);
                break;
            }
        }
        CHECK_TOK(tok, TOK_PIPE);
    } else {
        throw ParseErrorUnexpected(lex, tok, {TOK_PIPE, TOK_DOUBLE_PIPE, TOK_RWORD_MOVE, TOK_RWORD_USE, TOK_RWORD_STATIC});
    }

    auto rt = mkType(lex.typePool(), lex.pointSpan());
    if (lex.getTokenIf(TOK_THINARROW)) {
        rt = ParseType(lex);
    }

    auto code = ParseExpr0(lex);
    if (isAsync) {
        code = NEWNODE(ASTExprNodeAsyncBlock, ::std::move(code), isMove);
    }

    return NEWNODE(ASTExprNodeClosure, ::std::move(args), ::std::move(rt), ::std::move(code), isMove, isUse, isImmovable);
}

ASTExprNodeP ParseExprValInner(TokenStream& lex) {
    TRACE_FUNCTION;

    Token tok;
    ASTPath path;

    if (lex.lookahead(0) == TOK_INTERPOLATED_PATH && ((lex.lookahead(1) == TOK_RWORD_MOVE && lex.lookahead(2) == TOK_BRACE_OPEN) || lex.lookahead(1) == TOK_BRACE_OPEN)) {
        GET_TOK(tok, lex);
        if (tok.fragPath().isTrivial() && tok.fragPath().asTrivial() == "gen") {
            // Generators!
            bool isMove = lex.getTokenIf(TOK_RWORD_MOVE);
            return NEWNODE(ASTExprNodeGeneratorBlock, ParseExprBlockNode(lex, ASTExprNodeBlock::Type::Bare), mkType(lex.typePool(), lex.pointSpan()), isMove, false);
        }
        PUTBACK(tok, lex);
    }

    switch (GET_TOK(tok, lex)) {
        case TOK_BRACE_OPEN:
            PUTBACK(tok, lex);
            return ParseExprBlockNode(lex);

        case TOK_INTERPOLATED_EXPR:
        case TOK_INTERPOLATED_BLOCK:
            return tok.takeFragNode();

        // Return/break/continue/... also parsed here (but recurses back up to actually handle them)
        case TOK_RWORD_RETURN:
        case TOK_RWORD_BECOME:
        case TOK_RWORD_YIELD:
        case TOK_RWORD_CONTINUE:
        case TOK_RWORD_BREAK:
            PUTBACK(tok, lex);
            return ParseStmt(lex);

        case TOK_LIFETIME:
            PUTBACK(tok, lex);
            return ParseExprBlockLine(lex, nullptr);

        case TOK_RWORD_LOOP:
            return NEWNODE(ASTExprNodeLoop, "", ParseExprBlockNode(lex));
        case TOK_RWORD_WHILE:
            return ParseWhileStmt(lex, Ident(""));
        case TOK_RWORD_FOR:
            return ParseForStmt(lex, Ident(""));
        case TOK_RWORD_TRY: // Only emitted in 2018
            return ParseExprTry(lex);
        case TOK_RWORD_DO:
            GET_TOK(tok, lex);
            // `do catch` (1.29) - stabilised later as `try`
            if (tok.type() == TOK_IDENT && tok.ident().name == "catch") {
                return ParseExprTry(lex);
            }
            // `do yeet` (1.74) - Not stabilised (as of 2024-04)
            else if (tok.type() == TOK_IDENT && tok.ident().name == "yeet") {
                return ParseFlowControl(lex, ASTExprNodeFlow::YEET);
            } else {
                throw ParseErrorUnexpected(lex, tok);
            }
        case TOK_RWORD_MATCH:
            return ParseExprMatch(lex);
        case TOK_RWORD_IF:
            return ParseIfStmt(lex);
        case TOK_RWORD_ASYNC: {
            if (lex.lookahead(0) == TOK_PIPE || lex.lookahead(0) == TOK_DOUBLE_PIPE || (lex.lookahead(0) == TOK_RWORD_MOVE && (lex.lookahead(1) == TOK_PIPE || lex.lookahead(1) == TOK_DOUBLE_PIPE))) {
                return ParseExprValClosure(lex, true);
            }
            bool isMove = lex.getTokenIf(TOK_RWORD_MOVE);
            return NEWNODE(ASTExprNodeAsyncBlock, ParseExprBlockNode(lex, ASTExprNodeBlock::Type::Bare), isMove);
        }
        case TOK_RWORD_UNSAFE:
            return ParseExprBlockNode(lex, ASTExprNodeBlock::Type::Unsafe);
        case TOK_RWORD_CONST:
            return ParseExprBlockNode(lex, ASTExprNodeBlock::Type::Const);

        // Paths
        // `self` can be a value, or start a path
        case TOK_RWORD_SELF:
            if (LOOK_AHEAD(lex) != TOK_DOUBLE_COLON) {
                static const RcString rcstringSelfLower = RcString::newInterned("self");
                return NEWNODE(ASTExprNodeNamedValue, ASTPath(rcstringSelfLower));
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
            path = ParsePath(lex, PATH_GENERIC_EXPR);

            DEBUG("path = " << path << ", lookahead=" << Token::typestr(lex.lookahead(0)));
            switch (GET_TOK(tok, lex)) {
                case TOK_EXCLAM:
                    return ParseExprMacro(lex, mv$(path));
                case TOK_PAREN_OPEN:
                    // Function call
                    PUTBACK(tok, lex);
                    return NEWNODE(ASTExprNodeCallPath, ::std::move(path), ParseParenList(lex));
                case TOK_BRACE_OPEN:
                    if (!CHECK_PARSE_FLAG(lex, disallowStructLiteral)) {
                        return ParseExprValStructLiteral(lex, ::std::move(path));
                    } else {
                        DEBUG("Not parsing struct literal");
                    }
                    // Value
                    PUTBACK(tok, lex);
                    return NEWNODE(ASTExprNodeNamedValue, ::std::move(path));
                // `builtin # <name>` - seems to be a 1.74 era hack to extend syntax
                // - `offset_of` is translated to an intrinsic call with the fields as
                //   string/integer arguments. `type_ascribe` maps directly to the
                //   existing type-annotation expression.
                case TOK_HASH:
                    if (path.isTrivial() && path.asTrivial() == "builtin") {
                        GET_CHECK_TOK(tok, lex, TOK_IDENT);
                        if (tok.ident() == "offset_of") {
                            GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
                            auto ty = ParseType(lex);
                            std::vector<ASTExprNodeP> args;
                            do {
                                GET_CHECK_TOK(tok, lex, TOK_COMMA);
                                if (lex.lookahead(0) == TOK_INTERPOLATED_EXPR) {
                                    GET_CHECK_TOK(tok, lex, TOK_INTERPOLATED_EXPR);
                                    const auto* expr = &tok.fragNode();
                                    std::vector<ASTExprNodeP> exprArgs;
                                    for (;;) {
                                        if (const auto* n = cast<const ASTExprNodeNamedValue>(expr)) {
                                            exprArgs.push_back(NEWNODE(ASTExprNodeString, n->mPath.asTrivial().c_str(), {}));
                                            break;
                                        } else if (const auto* n = cast<const ASTExprNodeInteger>(expr)) {
                                            exprArgs.push_back(NEWNODE(ASTExprNodeInteger, n->mValue, n->datatype));
                                            break;
                                        } else if (const auto* n = cast<const ASTExprNodeFloat>(expr)) {
                                            const auto value = n->mValue;
                                            if (value < FloatValue() || floatValueIsNan(value) || floatValueIsInfinite(value)) {
                                                TODO(lex.pointSpan(), "offset_of - invalid tuple indices " << *expr);
                                            }
                                            // An integer index fits iff it is below SIZE_MAX + 1 = 2^64
                                            const auto indexLimit = FloatValue(18446744073709551616.0);
                                            const auto whole = floatValueTrunc(value);
                                            if (whole >= indexLimit) {
                                                TODO(lex.pointSpan(), "offset_of - tuple index is too large " << *expr);
                                            }

                                            const auto fraction = value - whole;
                                            auto scale = FloatValue(1.0);
                                            FloatValue fractionalIndex;
                                            const auto tolerancePerUnit = parseFloatValue("1e-32");
                                            bool foundFractionalIndex = false;
                                            for (unsigned digits = 0; digits < 20; digits++) {
                                                scale = scale * FloatValue(10.0);
                                                const auto scaled = fraction * scale;
                                                fractionalIndex = floatValueRound(scaled);
                                                if (floatValueAbs(scaled - fractionalIndex) <= scale * tolerancePerUnit) {
                                                    foundFractionalIndex = true;
                                                    break;
                                                }
                                            }
                                            if (!foundFractionalIndex || fractionalIndex >= indexLimit) {
                                                TODO(lex.pointSpan(), "offset_of - invalid tuple indices " << *expr);
                                            }
                                            exprArgs.push_back(NEWNODE(ASTExprNodeInteger, U128(static_cast<uint64_t>(fractionalIndex)), CORETYPE_ANY));
                                            exprArgs.push_back(NEWNODE(ASTExprNodeInteger, U128(static_cast<uint64_t>(whole)), CORETYPE_ANY));
                                            break;
                                        } else if (const auto* n = cast<const ASTExprNodeField>(expr)) {
                                            exprArgs.push_back(NEWNODE(ASTExprNodeString, n->mName.c_str(), {}));
                                            expr = &*n->obj;
                                        } else {
                                            TODO(lex.pointSpan(), "offset_of - " << *expr);
                                        }
                                    }
                                    while (!exprArgs.empty()) {
                                        args.push_back(std::move(exprArgs.back()));
                                        exprArgs.pop_back();
                                    }
                                } else if (lex.lookahead(0) == TOK_INTEGER) {
                                    GET_CHECK_TOK(tok, lex, TOK_INTEGER);
                                    args.push_back(NEWNODE(ASTExprNodeInteger, tok.intval(), tok.datatype()));
                                } else {
                                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                                    args.push_back(NEWNODE(ASTExprNodeString, tok.ident().name.c_str(), tok.ident().hygiene));
                                }
                            } while (lex.lookahead(0) == TOK_COMMA);
                            GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);

                            // TODO: How to emit this, maybe as a hacky intrinsic?
                            // ::"#intrinsics"::offset_of::<T>("field1",...)
                            // - Fiddly
                            path = ASTPath(RcString::newInterned("#intrinsics"), {ASTPathNode("offset_of")});
                            path.nodes().back().args().entries.push_back(std::move(ty));
                            return NEWNODE(ASTExprNodeCallPath, std::move(path), std::move(args));
                        } else if (tok.ident() == "type_ascribe") {
                            GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
                            auto value = ParseExpr0(lex);
                            GET_CHECK_TOK(tok, lex, TOK_COMMA);
                            auto ty = ParseType(lex);
                            GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                            return NEWNODE(ASTExprNodeTypeAnnotation, std::move(value), std::move(ty));
                        } else {
                            TODO(lex.pointSpan(), "`builtin #` support - " << tok.ident());
                        }
                    }
                default:
                    // Value
                    PUTBACK(tok, lex);
                    return NEWNODE(ASTExprNodeNamedValue, ::std::move(path));
            }
        // Closures
        case TOK_RWORD_STATIC:
        case TOK_RWORD_MOVE:
        case TOK_RWORD_USE:
        case TOK_PIPE:
        case TOK_DOUBLE_PIPE:
            PUTBACK(tok, lex);
            return ParseExprValClosure(lex);

        case TOK_UNDERSCORE:
            return NEWNODE(ASTExprNodeWildcardPattern);
        case TOK_INTEGER:
            return NEWNODE(ASTExprNodeInteger, tok.intval(), tok.datatype());
        case TOK_FLOAT:
            return NEWNODE(ASTExprNodeFloat, tok.floatval(), tok.datatype());
        case TOK_STRING:
            return NEWNODE(ASTExprNodeString, tok.str(), tok.strHygiene());
        case TOK_BYTESTRING:
            return NEWNODE(ASTExprNodeByteString, tok.str());
        case TOK_CSTRING:
            return NEWNODE(ASTExprNodeCString, tok.str());
        case TOK_RWORD_TRUE:
            return NEWNODE(ASTExprNodeBool, true);
        case TOK_RWORD_FALSE:
            return NEWNODE(ASTExprNodeBool, false);
        case TOK_PAREN_OPEN:
            if (lex.getTokenIf(TOK_PAREN_CLOSE)) {
                DEBUG("Unit");
                return NEWNODE(ASTExprNodeTuple, ::std::vector<ASTExprNodeP>());
            } else {
                CLEAR_PARSE_FLAGS_EXPR(lex);

                ASTExprNodeP rv = ParseExpr0(lex);
                if (GET_TOK(tok, lex) == TOK_COMMA) {
                    ::std::vector<ASTExprNodeP> ents;
                    ents.push_back(::std::move(rv));
                    do {
                        if (lex.getTokenIf(TOK_PAREN_CLOSE, tok)) {
                            break;
                        }
                        ents.push_back(ParseExpr0(lex));
                    } while (GET_TOK(tok, lex) == TOK_COMMA);
                    rv = NEWNODE(ASTExprNodeTuple, ::std::move(ents));
                }
                CHECK_TOK(tok, TOK_PAREN_CLOSE);
                return rv;
            }
        case TOK_SQUARE_OPEN:
            if (lex.getTokenIf(TOK_SQUARE_CLOSE)) {
                // Empty literal
                return NEWNODE(ASTExprNodeArray, ::std::vector<ASTExprNodeP>());
            } else {
                CLEAR_PARSE_FLAGS_EXPR(lex);
                auto first = ParseExpr0(lex);
                if (GET_TOK(tok, lex) == TOK_SEMICOLON) {
                    // Repetiion
                    auto count = ParseExpr0(lex);
                    GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                    return NEWNODE(ASTExprNodeArray, ::std::move(first), ::std::move(count));
                } else {
                    ::std::vector<ASTExprNodeP> items;
                    items.push_back(::std::move(first));
                    while (tok.type() == TOK_COMMA) {
                        if (GET_TOK(tok, lex) == TOK_SQUARE_CLOSE) {
                            break;
                        } else {
                            PUTBACK(tok, lex);
                        }
                        items.push_back(ParseExpr0(lex));
                        GET_TOK(tok, lex);
                    }
                    CHECK_TOK(tok, TOK_SQUARE_CLOSE);
                    return NEWNODE(ASTExprNodeArray, ::std::move(items));
                }
            }
            throw CompileErrorBugCheck(lex, "Array literal fell");
        default:
            throw ParseErrorUnexpected(lex, tok);
    }
}

ASTExprNodeP ParseExprVal(TokenStream& lex) {
    auto attrs = ParseItemAttrs(lex);
    auto rv = ParseExprValInner(lex);
    rv->setAttrs(std::move(attrs));
    return rv;
}

ASTExprNodeP ParseExprMacro(TokenStream& lex, ASTPath path) {
    Token tok;
    auto definitionHygiene = lex.getHygiene();

    RcString ident;
    if (lex.getTokenIf(TOK_IDENT, tok)) {
        ident = tok.ident().name;
    }

    bool isMacro = (path.isTrivial() && path.asTrivial() == "macro_rules");

    bool isBraced = lex.lookahead(0) == TOK_BRACE_OPEN;

    if (isMacro) {
        lex.pushHygine();
    }
    TokenTree tt = ParseTT(lex, true);
    if (tt.isToken()) {
        throw ParseErrorUnexpected(lex, tt.tok());
    }
    if (isMacro) {
        lex.popHygine();
    }

    DEBUG("name=" << path << ", ident=" << ident << ", tt=" << tt);
    return NEWNODE(ASTExprNodeMacro, mv$(path), mv$(ident), mv$(tt), isBraced, mv$(definitionHygiene));
}

// Token Tree Parsing
TokenTree ParseTT(TokenStream& lex, bool unwrapped) {
    TokenTree rv;
    TRACE_FUNCTION_FR("", rv);

    auto edition = lex.getEdition();
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
            throw ParseErrorUnexpected(lex, tok);
        default:
            rv = TokenTree(edition, lex.getHygiene(), mv$(tok));
            DEBUG(rv);
            return rv;
    }

    ::std::vector<TokenTree> items;
    if (!unwrapped) {
        items.push_back(TokenTree(edition, lex.getHygiene(), mv$(tok)));
    }
    while (!lex.getTokenIf(closer, tok) && !lex.getTokenIf(TOK_EOF, tok)) {
        if (lex.lookahead(0) == TOK_NULL) {
            throw ParseErrorUnexpected(lex, lex.getToken());
        }
        items.push_back(ParseTT(lex, false));
    }
    if (!unwrapped) {
        items.push_back(TokenTree(lex.getEdition(), lex.getHygiene(), mv$(tok)));
    }
    rv = TokenTree(edition, lex.getHygiene(), mv$(items));
    DEBUG(rv);
    return rv;
}

#undef NEWNODE
#undef LEFTASSOC

ASTPath ParsePath(TokenStream& lex, eParsePathGenericMode genericMode);
ASTPath ParsePath(TokenStream& lex, bool isAbs, eParsePathGenericMode genericMode);
::std::vector<ASTPathNode> ParsePathNodes(TokenStream& lex, eParsePathGenericMode genericMode);
ASTPathParams ParsePathGenericList(TokenStream& lex);
ASTHigherRankedBounds ParseHRBOpt(TokenStream& lex);

ASTPath ParsePath(TokenStream& lex, eParsePathGenericMode genericMode) {
    TRACE_FUNCTION_F("generic_mode=" << genericMode);

    Token tok;
    switch (GET_TOK(tok, lex)) {
        case TOK_INTERPOLATED_PATH:
            return mv$(tok.fragPath());

        case TOK_RWORD_SELF:
            if (lex.lookahead(0) != TOK_DOUBLE_COLON) {
                return ASTPath::newSelf({});
            }
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            return ASTPath::newSelf(ParsePathNodes(lex, genericMode));

        case TOK_RWORD_SUPER: {
            if (lex.lookahead(0) != TOK_DOUBLE_COLON) {
                return ASTPath::newSuper(1, {});
            }
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            unsigned int count = 1;
            while (LOOK_AHEAD(lex) == TOK_RWORD_SUPER) {
                count += 1;
                GET_TOK(tok, lex);
                if (lex.lookahead(0) != TOK_DOUBLE_COLON) {
                    return ASTPath::newSuper(count, {});
                }
                GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            }
            return ASTPath::newSuper(count, ParsePathNodes(lex, genericMode));
        }

        case TOK_RWORD_CRATE:
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            return ParsePath(lex, true, genericMode);
        case TOK_DOUBLE_COLON:
            if (lex.lookahead(0) == TOK_STRING) {
            }
            // QUIRK: `::crate::foo` is valid (semi-surprisingly)
            // TODO: Reference?
            else if (lex.lookahead(0) == TOK_RWORD_CRATE) {
            } else if (lex.editionAfter(ASTEdition::Rust2018)) {
                // The first component is a crate name
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                // Internal AST encoding: `=crate` denotes a Rust 2018 extern-prelude absolute path.
                auto crateName = RcString(std::string("=") + tok.ident().name.c_str());
                std::vector<ASTPathNode> nodes;
                if (lex.lookahead(0) == TOK_DOUBLE_COLON) {
                    GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
                    nodes = ParsePathNodes(lex, genericMode);
                }
                return ASTPath(crateName, ::std::move(nodes));
            }
            return ParsePath(lex, true, genericMode);

        //case TOK_THINARROW_LEFT:
        case TOK_DOUBLE_LT:
            lex.putback(Token(TOK_LT));
        case TOK_LT: {
            ASTType* ty = ParseType(lex, true); // Allow trait objects without parens
            if (GET_TOK(tok, lex) == TOK_RWORD_AS) {
                ASTPath trait = ParsePath(lex, PATH_GENERIC_TYPE);
                GET_CHECK_TOK(tok, lex, TOK_GT);
                GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
                return ASTPath::newUfcsTrait(mv$(ty), mv$(trait), ParsePathNodes(lex, genericMode));
            } else {
                PUTBACK(tok, lex);
                GET_CHECK_TOK(tok, lex, TOK_GT);
                // TODO: Terminating the "path" here is sometimes valid?
                GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
                // NOTE: <Foo>::BAR is actually `<Foo as _>::BAR` (in mrustc parleance)
                return ASTPath::newUfcsTy(mv$(ty), ParsePathNodes(lex, genericMode));
            }
            throw "";
        }

        default:
            PUTBACK(tok, lex);
            return ParsePath(lex, false, genericMode);
    }
}

ASTPath ParsePath(TokenStream& lex, bool isAbs, eParsePathGenericMode genericMode) {
    Token tok;
    if (isAbs) {
        // QUIRK: `::crate::foo` is valid (semi-surprisingly)
        if (lex.getTokenIf(TOK_RWORD_CRATE)) {
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            return ASTPath("", ParsePathNodes(lex, genericMode));
        } else if (lex.getTokenIf(TOK_STRING, tok)) {
            auto cratename = RcString::newInterned(tok.str());
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            return ASTPath(cratename, ParsePathNodes(lex, genericMode));
        } else {
            return ASTPath("", ParsePathNodes(lex, genericMode));
        }
    } else {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto hygine = tok.ident().hygiene;
        DEBUG("hygine = " << hygine);
        PUTBACK(tok, lex);
        return ASTPath::newRelative(mv$(hygine), ParsePathNodes(lex, genericMode));
    }
}

::std::vector<ASTPathNode> ParsePathNodes(TokenStream& lex, eParsePathGenericMode genericMode) {
    TRACE_FUNCTION_F("generic_mode=" << genericMode);

    Token tok;
    ::std::vector<ASTPathNode> ret;

    while (true) {
        ASTPathParams params;

        GET_TOK(tok, lex);
        RcString component;
        if (tok.type() == TOK_IDENT) {
            component = mv$(tok.ident().name);
        } else if (tok.type() == TOK_RWORD_SELF || tok.type() == TOK_RWORD_SUPER || tok.type() == TOK_RWORD_CRATE) {
            component = RcString::newInterned(tok.toStr());
        } else {
            throw ParseErrorUnexpected(lex, tok, TOK_IDENT);
        }

        if (genericMode == PATH_GENERIC_TYPE) {
            // If `foo::<` is seen in type context, then consume the `::` and continue on.
            if (lex.lookahead(0) == TOK_DOUBLE_COLON && (lex.lookahead(1) == TOK_LT || lex.lookahead(1) == TOK_DOUBLE_LT || lex.lookahead(1) == TOK_THINARROW_LEFT)) {
                GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            }
            if (lex.lookahead(0) == TOK_LT || lex.lookahead(0) == TOK_DOUBLE_LT || lex.lookahead(0) == TOK_THINARROW_LEFT) {
                GET_TOK(tok, lex);
                // The lexer maximally tokenizes `<<`; generics consume the second `<` separately.
                if (tok.type() == TOK_DOUBLE_LT) {
                    lex.putback(Token(TOK_LT));
                }
                if (tok.type() == TOK_THINARROW_LEFT) {
                    lex.putback(Token(TOK_DASH));
                }

                // Type-mode generics "::path::to::Type<A,B>"
                params = ParsePathGenericList(lex);
            }
            // Parenthesized arguments encode the `Fn(A, B) -> C` trait-path syntax.
            else if (lex.lookahead(0) == TOK_PAREN_OPEN) {
                auto ps = lex.startSpan();
                GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
                if (lex.lookahead(0) == TOK_DOUBLE_DOT) {
                    GET_CHECK_TOK(tok, lex, TOK_DOUBLE_DOT);
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    params.isRtn = true;
                    DEBUG("return-type notation (..)");
                } else {
                    DEBUG("Fn() parenthesized arguments");
                    ::std::vector<ASTType*> args;
                    do {
                        // Trailing comma or empty list support
                        if (lex.lookahead(0) == TOK_PAREN_CLOSE) {
                            GET_TOK(tok, lex);
                            break;
                        }
                        args.push_back(ParseType(lex));
                    } while (GET_TOK(tok, lex) == TOK_COMMA);
                    CHECK_TOK(tok, TOK_PAREN_CLOSE);

                    ASTType* retType = mkType(lex.typePool(), ASTTypeTags::Unit(), lex.pointSpan());
                    if (lex.lookahead(0) == TOK_THINARROW) {
                        GET_TOK(tok, lex);
                        retType = ParseType(lex, false);
                    }
                    DEBUG("- Fn(" << args << ")->" << retType << "");

                    // Encode into path, by converting Fn(A,B)->C into Fn<(A,B),Ret=C>
                    params = ASTPathParams();
                    params.isParen = true;
                    params.entries.push_back(mkType(lex.typePool(), ASTTypeTags::Tuple(), lex.endSpan(ps), mv$(args)));
                    params.entries.push_back(::std::make_pair(ASTPathNode(RcString::newInterned("Output")), mv$(retType)));
                }
            } else {
            }
        }
        if (lex.lookahead(0) != TOK_DOUBLE_COLON) {
            ret.push_back(ASTPathNode(component, mv$(params)));
            break;
        }
        GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
        if (genericMode == PATH_GENERIC_EXPR && (lex.lookahead(0) == TOK_LT || lex.lookahead(0) == TOK_DOUBLE_LT || lex.lookahead(0) == TOK_THINARROW_LEFT)) {
            GET_TOK(tok, lex);
            // The lexer maximally tokenizes `<<`; turbofish generics consume the second `<` separately.
            if (tok.type() == TOK_DOUBLE_LT) {
                lex.putback(Token(TOK_LT));
            }
            if (tok.type() == TOK_THINARROW_LEFT) {
                lex.putback(Token(TOK_DASH));
            }

            // Expr-mode generics "::path::to::function::<Type1,Type2>(arg1, arg2)"
            params = ParsePathGenericList(lex);
            if (lex.lookahead(0) != TOK_DOUBLE_COLON) {
                ret.push_back(ASTPathNode(component, mv$(params)));
                // Break out of loop down to return
                break;
            }
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
        }
        ret.push_back(ASTPathNode(component, mv$(params)));
    }
    DEBUG("ret = " << ret);
    return ret;
}

/// Parse a list of parameters within a path
ASTPathParams ParsePathGenericList(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    ASTPathParams rv;

    do {
        if (LOOK_AHEAD(lex) == TOK_GT || LOOK_AHEAD(lex) == TOK_DOUBLE_GT || LOOK_AHEAD(lex) == TOK_GTE || LOOK_AHEAD(lex) == TOK_DOUBLE_GT_EQUAL) {
            GET_TOK(tok, lex);
            break;
        }
        switch (GET_TOK(tok, lex)) {
            case TOK_LIFETIME:
                rv.entries.push_back(ASTLifetimeRef(/*lex.point_span(),*/ tok.ident()));
                break;
            case TOK_RWORD_TRUE:
            case TOK_RWORD_FALSE:
            case TOK_DASH:
            case TOK_INTEGER:
            case TOK_FLOAT:
            case TOK_INTERPOLATED_EXPR:
            case TOK_BRACE_OPEN:
                PUTBACK(tok, lex);
                rv.entries.push_back(ParseExpr13(lex));
                break;
            default:
                PUTBACK(tok, lex);
                rv.entries.push_back(ParseType(lex));
                if (lex.lookahead(0) == TOK_EQUAL || lex.lookahead(0) == TOK_COLON) {
                    auto sp = lex.pointSpan();
                    // Uh-oh, the previously-parsed type was actually an ATY name (with generics, probably)
                    // - Decode the above type into the name
                    auto& ty = rv.entries.back().as_Type();
                    if (!ty->isPath()) {
                        ERROR(sp, E0000, "Unexpected = or : after non-trivial type path - " << ty);
                    }
                    auto& p = ty->path();
                    if (!p.cls.is_Relative()) {
                        ERROR(sp, E0000, "Unexpected = or : after non-trivial type path - " << ty);
                    }
                    if (p.cls.as_Relative().nodes.size() != 1) {
                        ERROR(sp, E0000, "Unexpected = or : after non-trivial type path - " << ty);
                    }
                    auto n = std::move(p.cls.as_Relative().nodes[0]);
                    rv.entries.pop_back();
                    if (lex.getTokenIf(TOK_EQUAL)) {
                        rv.entries.push_back(::std::make_pair(mv$(n), ParseType(lex, false)));
                    } else if (lex.getTokenIf(TOK_COLON)) {
                        std::vector<TypeTraitPath> traits;
                        // TODO: Trait list instead of duplicating the name
                        for (;;) {
                            // Region bounds are checked by the borrow checker and are
                            // otherwise erased from the HIR.
                            if (lex.lookahead(0) == TOK_LIFETIME) {
                                GET_TOK(tok, lex);
                            } else {
                                auto hrbs = ParseHRBOpt(lex);
                                traits.push_back(TypeTraitPath(mv$(hrbs), ParsePath(lex, PATH_GENERIC_TYPE)));
                            }
                            if (lex.lookahead(0) != TOK_PLUS) {
                                break;
                            }
                            GET_CHECK_TOK(tok, lex, TOK_PLUS);
                            // Allow trailing `+`
                            if (lex.lookahead(0) == TOK_COMMA || lex.lookahead(0) == TOK_PAREN_CLOSE || lex.lookahead(0) == TOK_GT) {
                                break;
                            }
                        }
                        rv.entries.push_back(::std::make_pair(mv$(n), std::move(traits)));
                    } else {
                        throw "Unreachable";
                    }
                }
                break;
        }
    } while (GET_TOK(tok, lex) == TOK_COMMA);

    // The lexer maximally tokenizes closing `>>`/`>=` operators; consume one generic-list `>` here.
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

// NEWNODE is needed for the Value pattern type
typedef ::std::unique_ptr<ASTExprNode> PatternExprNodeP;
#define NEWNODE(type, ...) PatternExprNodeP(new type(__VA_ARGS__))

ASTPattern ParsePattern1(TokenStream& lex, AllowOrPattern allowOr);
ASTPattern::Value ParsePatternValue(TokenStream& lex);
ASTPattern::TuplePat ParsePatternTuple(TokenStream& lex, bool* maybeJustParen = nullptr);
ASTPattern ParsePatternRealSlice(TokenStream& lex);
ASTPattern ParsePatternRealPath(TokenStream& lex, ProtoSpan ps, ASTPath path);
ASTPattern ParsePatternStruct(TokenStream& lex, ProtoSpan ps, ASTPath path);

ASTPattern ParsePatternReal(TokenStream& lex, AllowOrPattern allowOr);
ASTPattern ParsePatternReal1(TokenStream& lex, AllowOrPattern allowOr);

/// Parse a pattern
///
/// Examples:
/// - `Enum::Variant(a)`
/// - `(1, a)`
/// - `1 ... 2`
/// - `"string"`
/// - `mut x`
/// - `mut x @ 1 ... 2`
ASTPattern ParsePattern(TokenStream& lex, AllowOrPattern allowOr) {
    auto ps = lex.startSpan();
    if (allowOr == AllowOrPattern::Yes) {
        lex.getTokenIf(TOK_PIPE);
    }
    auto rv = ParsePattern1(lex, allowOr);
    if (allowOr == AllowOrPattern::Yes && lex.lookahead(0) == TOK_PIPE) {
        // NOTE: Legal for refutable positions (as long as all possibilities are covered)
        std::vector<ASTPattern> pats;
        pats.push_back(std::move(rv));
        while (lex.lookahead(0) == TOK_PIPE) {
            lex.getToken();
            pats.push_back(ParsePattern1(lex, allowOr));
        }
        return ASTPattern(lex.endSpan(ps), ASTPattern::Data::make_Or(mv$(pats)));
    } else {
        return rv;
    }
}

ASTPattern ParsePattern1(TokenStream& lex, AllowOrPattern allowOr) {
    TRACE_FUNCTION;
    auto ps = lex.startSpan();

    Token tok;
    tok = lex.getToken();

    // TODO: Why is this here explicitly?
    if (tok.type() == TOK_IDENT && lex.lookahead(0) == TOK_EXCLAM) {
        lex.getToken();
        if (tok.ident().name == "deref") {
            GET_TOK(tok, lex);
            CHECK_TOK(tok, TOK_PAREN_OPEN);
            auto sub = ParsePattern(lex, AllowOrPattern::Yes);
            GET_TOK(tok, lex);
            CHECK_TOK(tok, TOK_PAREN_CLOSE);
            return ASTPattern(ASTPattern::TagDeref(), lex.endSpan(ps), mv$(sub));
        }
        return ASTPattern(ASTPattern::TagMacro(), lex.endSpan(ps), box$(ParseMacroInvocation(ps, tok.ident().name, lex)));
    }
    if (tok.type() == TOK_INTERPOLATED_PATTERN) {
        return mv$(tok.fragPattern());
    }

    bool expectBind = false;
    auto bindType = ASTPatternBinding::Type::MOVE;
    bool isMut = false;
    // 1. Mutablity + Reference
    if (tok.type() == TOK_RWORD_REF) {
        expectBind = true;
        tok = lex.getToken();
        if (tok.type() == TOK_RWORD_MUT) {
            bindType = ASTPatternBinding::Type::MUTREF;
            GET_TOK(tok, lex);
        } else {
            bindType = ASTPatternBinding::Type::REF;
        }
    } else if (tok.type() == TOK_RWORD_MUT) {
        isMut = true;
        expectBind = true;
        GET_TOK(tok, lex);
    } else {
        // Fall through
    }

    ASTPatternBinding binding;
    ASTPattern pat;
    // If a 'ref' or 'mut' annotation was seen, the next name must be a binding name
    if (expectBind) {
        CHECK_TOK(tok, TOK_IDENT);
        auto bindName = tok.ident();
        // If there's no '@' after it, it's a name binding only (_ pattern)
        if (GET_TOK(tok, lex) != TOK_AT) {
            PUTBACK(tok, lex);
            return ASTPattern(ASTPattern::TagBind(), lex.endSpan(ps), mv$(bindName), bindType, isMut);
        }
        binding = ASTPatternBinding(mv$(bindName), bindType, isMut);

        // '@' consumed, move on to next token
        pat = ParsePattern1(lex, allowOr);
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
                pat = ParsePatternReal(lex, allowOr);
                break;
            // Known binding `ident @`
            case TOK_AT:
                binding = ASTPatternBinding(tok.ident(), bindType /*MOVE*/, isMut /*false*/);
                GET_TOK(tok, lex); // '@'
                pat = ParsePattern1(lex, allowOr);
                break;
            default: { // Maybe bind
                auto name = tok.ident();
                // if the pattern can be refuted (i.e this could be an enum variant), return MaybeBind
                if (true /*is_refutable*/) {
                    assert(bindType == ASTPatternBinding::Type::MOVE);
                    assert(isMut == false);
                    return ASTPattern(ASTPattern::TagMaybeBind(), lex.endSpan(ps), mv$(name));
                }
                // Otherwise, it IS a binding
                else {
                    return ASTPattern(ASTPattern::TagBind(), lex.endSpan(ps), mv$(name), bindType, isMut);
                }
                throw "";
            }
        }
    } else {
        // Otherwise, fall through
        PUTBACK(tok, lex);
        pat = ParsePatternReal(lex, allowOr);
    }
    if (binding.isValid()) {
        pat.bindings().insert(pat.bindings().begin(), mv$(binding));
    }
    return pat;
}

ASTPattern ParsePatternReal(TokenStream& lex, AllowOrPattern allowOr) {
    Token tok;
    if (LOOK_AHEAD(lex) == TOK_INTERPOLATED_PATTERN) {
        GET_TOK(tok, lex);
        return mv$(tok.fragPattern());
    }
    auto ps = lex.startSpan();
    ASTPattern ret = ParsePatternReal1(lex, allowOr);
    if ((GET_TOK(tok, lex) == TOK_TRIPLE_DOT) || tok.type() == TOK_DOUBLE_DOT_EQUAL) {
        if (!ret.data().is_Value()) {
            throw CompileErrorGeneric(lex, "Using '...' with a non-value on left");
        }
        auto& retV = ret.data().as_Value();
        auto leftval = std::move(retV.start);

        auto rightval = ParsePatternValue(lex);
        if (rightval.is_Invalid()) {
            throw CompileErrorGeneric(lex, "Using '...' with a no RHS value");
        }

        return ASTPattern(lex.endSpan(ps), ASTPattern::Data::make_Value({mv$(leftval), mv$(rightval)}));
    } else if (tok.type() == TOK_DOUBLE_DOT) {
        if (!ret.data().is_Value()) {
            throw CompileErrorGeneric(lex, "Using `..` with a non-value on left");
        }
        auto& retV = ret.data().as_Value();
        auto leftval = std::move(retV.start);

        auto rightval = ParsePatternValue(lex);
        if (rightval.is_Invalid()) {
            // Right-open range!
            // - Perfectly valid
        }

        return ASTPattern(lex.endSpan(ps), ASTPattern::Data::make_ValueLeftInc({mv$(leftval), mv$(rightval)}));
    } else {
        PUTBACK(tok, lex);
        return ret;
    }
}

ASTPattern::Value ParsePatternValue(TokenStream& lex) {
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
            return ASTPattern::Value::make_Named(ParsePath(lex, PATH_GENERIC_EXPR));

        case TOK_DASH:
            if (GET_TOK(tok, lex) == TOK_INTEGER) {
                auto dt = tok.datatype();
                // TODO: Ensure that the type is ANY or a signed integer
                return ASTPattern::Value::make_Integer({dt, ~tok.intval() + 1u});
            } else if (tok.type() == TOK_FLOAT) {
                return ASTPattern::Value::make_Float({tok.datatype(), -tok.floatval()});
            } else {
                throw ParseErrorUnexpected(lex, tok, {TOK_INTEGER, TOK_FLOAT});
            }
        case TOK_FLOAT:
            return ASTPattern::Value::make_Float({tok.datatype(), tok.floatval()});
        case TOK_INTEGER:
            return ASTPattern::Value::make_Integer({tok.datatype(), tok.intval()});
        case TOK_RWORD_TRUE:
            return ASTPattern::Value::make_Integer({CORETYPE_BOOL, U128(1)});
        case TOK_RWORD_FALSE:
            return ASTPattern::Value::make_Integer({CORETYPE_BOOL, U128(0)});
        case TOK_STRING:
            return ASTPattern::Value::make_String(mv$(tok.str()));
        case TOK_BYTESTRING:
            return ASTPattern::Value::make_ByteString({mv$(tok.str())});
        case TOK_INTERPOLATED_EXPR: {
            auto e = tok.takeFragNode();
            // TODO: Visitor?
            if (auto* n = cast<ASTExprNodeString>(e.get())) {
                return ASTPattern::Value::make_String(mv$(n->mValue));
            } else if (auto* n = cast<ASTExprNodeByteString>(e.get())) {
                return ASTPattern::Value::make_ByteString({mv$(n->mValue)});
            } else if (auto* n = cast<ASTExprNodeBool>(e.get())) {
                return ASTPattern::Value::make_Integer({CORETYPE_BOOL, U128(n->mValue ? 1 : 0)});
            } else if (auto* n = cast<ASTExprNodeInteger>(e.get())) {
                return ASTPattern::Value::make_Integer({n->datatype, n->mValue});
            } else if (auto* n = cast<ASTExprNodeFloat>(e.get())) {
                return ASTPattern::Value::make_Float({n->datatype, n->mValue});
            } else {
                TODO(lex.pointSpan(), "Convert :expr into a pattern value - " << *e);
            }
        } break;
        default:
            PUTBACK(tok, lex);
            return ASTPattern::Value::make_Invalid({});
    }
}

ASTPattern ParsePatternReal1(TokenStream& lex, AllowOrPattern allowOr) {
    TRACE_FUNCTION;
    auto ps = lex.startSpan();

    Token tok;
    ASTPath path;

    switch (GET_TOK(tok, lex)) {
        case TOK_UNDERSCORE:
            return ASTPattern(lex.endSpan(ps), ASTPattern::Data());
        //case TOK_DOUBLE_DOT:
        case TOK_RWORD_BOX:
            return ASTPattern(ASTPattern::TagBox(), lex.endSpan(ps), ParsePattern1(lex, allowOr));
        case TOK_DOUBLE_AMP:
            lex.putback(TOK_AMP);
        case TOK_AMP: {
            DEBUG("Ref");
            // NOTE: Falls back into "Pattern" not "PatternReal" to handle MaybeBind again
            bool isMut = false;
            if (GET_TOK(tok, lex) == TOK_RWORD_MUT) {
                isMut = true;
            } else {
                PUTBACK(tok, lex);
            }
            return ASTPattern(ASTPattern::TagReference(), lex.endSpan(ps), isMut, ParsePattern1(lex, allowOr));
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
            return ParsePatternRealPath(lex, ps, ParsePath(lex, PATH_GENERIC_EXPR));
        case TOK_DOUBLE_DOT_EQUAL:
        case TOK_TRIPLE_DOT:
            return ASTPattern(lex.endSpan(ps), ASTPattern::Data::make_Value({{}, ParsePatternValue(lex)}));
        case TOK_DOUBLE_DOT:
            return ASTPattern(lex.endSpan(ps), ASTPattern::Data::make_ValueLeftInc({{}, ParsePatternValue(lex)}));
        case TOK_DASH:
        case TOK_FLOAT:
        case TOK_INTEGER:
        case TOK_RWORD_TRUE:
        case TOK_RWORD_FALSE:
        case TOK_STRING:
        case TOK_BYTESTRING:
        case TOK_INTERPOLATED_EXPR:
            PUTBACK(tok, lex);
            return ASTPattern(ASTPattern::TagValue(), lex.endSpan(ps), ParsePatternValue(lex));

        case TOK_PAREN_OPEN: {
            bool justParen = false;
            auto tpat = ParsePatternTuple(lex, &justParen);
            // If it was `(<pat>)` (and not `(<pat>,)`) then unwrap to the first element
            if (justParen) {
                assert(tpat.start.size() == 1);
                assert(!tpat.hasWildcard);
                assert(tpat.end.size() == 0);
                return std::move(tpat.start.front());
            }
            return ASTPattern(ASTPattern::TagTuple(), lex.endSpan(ps), std::move(tpat));
        }
        case TOK_SQUARE_OPEN:
            return ParsePatternRealSlice(lex);
        default:
            throw ParseErrorUnexpected(lex, tok);
    }
    throw "unreachable";
}

ASTPattern ParsePatternRealPath(TokenStream& lex, ProtoSpan ps, ASTPath path) {
    Token tok;

    switch (GET_TOK(tok, lex)) {
        case TOK_PAREN_OPEN:
            return ASTPattern(ASTPattern::TagNamedTuple(), lex.endSpan(ps), mv$(path), ParsePatternTuple(lex, nullptr));
        case TOK_BRACE_OPEN:
            return ParsePatternStruct(lex, ps, mv$(path));
        default:
            PUTBACK(tok, lex);
            return ASTPattern(ASTPattern::TagValue(), lex.endSpan(ps), ASTPattern::Value::make_Named(mv$(path)));
    }
}

ASTPattern ParsePatternRealSlice(TokenStream& lex) {
    auto ps = lex.startSpan();
    Token tok;

    ::std::vector<ASTPattern> leading;
    ::std::vector<ASTPattern> trailing;
    ASTPatternBinding innerBinding;
    bool isSplit = false;

    while (GET_TOK(tok, lex) != TOK_SQUARE_CLOSE) {
        bool hasBinding = true;
        ASTPatternBinding binding;
        // `ref [mut] foo ..` or `ref [mut] foo @ ..`
        if (tok.type() == TOK_RWORD_REF && ((lex.lookahead(0) == TOK_IDENT && (lex.lookahead(1) == TOK_DOUBLE_DOT || (lex.lookahead(1) == TOK_AT && lex.lookahead(2) == TOK_DOUBLE_DOT))) || (lex.lookahead(0) == TOK_RWORD_MUT && lex.lookahead(1) == TOK_IDENT && (lex.lookahead(2) == TOK_DOUBLE_DOT || (lex.lookahead(2) == TOK_AT && lex.lookahead(3) == TOK_DOUBLE_DOT))))) {
            auto bindingType = ASTPatternBinding::Type::REF;
            if (lex.lookahead(0) == TOK_RWORD_MUT) {
                GET_CHECK_TOK(tok, lex, TOK_RWORD_MUT);
                bindingType = ASTPatternBinding::Type::MUTREF;
            }
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            binding = ASTPatternBinding(tok.ident(), bindingType, false);
        }
        // `foo ..` or `foo @ ..`
        else if (tok.type() == TOK_IDENT && (lex.lookahead(0) == TOK_DOUBLE_DOT || (lex.lookahead(0) == TOK_AT && lex.lookahead(1) == TOK_DOUBLE_DOT))) {
            binding = ASTPatternBinding(tok.ident(), ASTPatternBinding::Type::MOVE, false);
        }
        // `_ ..` or `_ @ ..`
        else if (tok.type() == TOK_UNDERSCORE && (lex.lookahead(0) == TOK_DOUBLE_DOT || (lex.lookahead(0) == TOK_AT && lex.lookahead(1) == TOK_DOUBLE_DOT))) {
            // No binding, but switching to trailing
        } else if (tok.type() == TOK_DOUBLE_DOT) {
            // No binding, but switching to trailing
            PUTBACK(tok, lex);
        } else {
            hasBinding = false;
        }

        if (hasBinding) {
            if (isSplit) {
                ERROR(lex.endSpan(ps), E0000, "Multiple instances of .. in a slice pattern");
            }

            innerBinding = mv$(binding);
            isSplit = true;
            if (lex.lookahead(0) == TOK_AT) {
                GET_CHECK_TOK(tok, lex, TOK_AT);
            }
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_DOT);
        } else {
            PUTBACK(tok, lex);
            if (!isSplit) {
                leading.push_back(ParsePattern(lex));
            } else {
                trailing.push_back(ParsePattern(lex));
            }
        }

        if (GET_TOK(tok, lex) != TOK_COMMA) {
            break;
        }
    }
    CHECK_TOK(tok, TOK_SQUARE_CLOSE);

    if (isSplit) {
        return ASTPattern(lex.endSpan(ps), ASTPattern::Data::make_SplitSlice({mv$(leading), mv$(innerBinding), mv$(trailing)}));
    } else {
        assert(!innerBinding.isValid());
        assert(trailing.empty());
        return ASTPattern(lex.endSpan(ps), ASTPattern::Data::make_Slice({mv$(leading)}));
    }
}

ASTPattern::TuplePat ParsePatternTuple(TokenStream& lex, bool* justParen) {
    TRACE_FUNCTION;
    auto sp = lex.startSpan();
    Token tok;
    if (justParen) {
        *justParen = false;
    }

    ::std::vector<ASTPattern> leading;
    while (LOOK_AHEAD(lex) != TOK_PAREN_CLOSE && LOOK_AHEAD(lex) != TOK_DOUBLE_DOT) {
        leading.push_back(ParsePattern(lex));

        if (GET_TOK(tok, lex) != TOK_COMMA) {
            CHECK_TOK(tok, TOK_PAREN_CLOSE);
            // If this was just a parenthesised pattern, then indicate to the caller
            if (justParen) {
                *justParen = (leading.size() == 1);
            }
            return ASTPattern::TuplePat{mv$(leading), false, {}};
        }
    }

    if (LOOK_AHEAD(lex) != TOK_DOUBLE_DOT) {
        GET_TOK(tok, lex);

        CHECK_TOK(tok, TOK_PAREN_CLOSE);
        return ASTPattern::TuplePat{mv$(leading), false, {}};
    }
    GET_CHECK_TOK(tok, lex, TOK_DOUBLE_DOT);

    ::std::vector<ASTPattern> trailing;
    if (GET_TOK(tok, lex) == TOK_COMMA) {
        while (LOOK_AHEAD(lex) != TOK_PAREN_CLOSE) {
            trailing.push_back(ParsePattern(lex));

            if (GET_TOK(tok, lex) != TOK_COMMA) {
                PUTBACK(tok, lex);
                break;
            }
        }
        GET_TOK(tok, lex);
    }

    CHECK_TOK(tok, TOK_PAREN_CLOSE);
    return ASTPattern::TuplePat{mv$(leading), true, mv$(trailing)};
}

ASTPattern ParsePatternStruct(TokenStream& lex, ProtoSpan ps, ASTPath path) {
    TRACE_FUNCTION;
    Token tok;

    // #![feature(relaxed_adts)]
    if (LOOK_AHEAD(lex) == TOK_INTEGER) {
        bool splitAllowed = false;
        ::std::map<unsigned int, ASTPattern> pats;
        while (GET_TOK(tok, lex) == TOK_INTEGER) {
            unsigned int ofs = static_cast<unsigned int>(tok.intval().truncateU64());
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto val = ParsePattern(lex);
            if (!pats.insert(::std::make_pair(ofs, mv$(val))).second) {
                ERROR(lex.pointSpan(), E0000, "Duplicate index");
            }

            if (GET_TOK(tok, lex) == TOK_BRACE_CLOSE) {
                break;
            }
            CHECK_TOK(tok, TOK_COMMA);
        }
        if (tok.type() == TOK_DOUBLE_DOT) {
            splitAllowed = true;
            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_BRACE_CLOSE);

        bool hasSplit = false;
        ::std::vector<ASTPattern> leading;
        ::std::vector<ASTPattern> trailing;
        unsigned int i = 0;
        for (auto& p : pats) {
            if (p.first != i) {
                if (hasSplit || !splitAllowed) {
                    ERROR(lex.pointSpan(), E0000, "Missing index " << i);
                }
                hasSplit = true;
                i = p.first;
            }
            if (!hasSplit) {
                leading.push_back(mv$(p.second));
            } else {
                trailing.push_back(mv$(p.second));
            }
            i++;
        }

        return ASTPattern(ASTPattern::TagNamedTuple(), lex.endSpan(ps), mv$(path), ASTPattern::TuplePat{mv$(leading), hasSplit, mv$(trailing)});
    }

    bool isExhaustive = true;
    ::std::vector<ASTStructPatternEntry> subpats;
    do {
        if (lex.lookahead(0) == TOK_BRACE_CLOSE) {
            GET_TOK(tok, lex);
            break;
        }
        if (lex.lookahead(0) == TOK_DOUBLE_DOT) {
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_DOT);
            isExhaustive = false;
            GET_TOK(tok, lex);
            break;
        }

        auto attrs = ParseItemAttrs(lex);

        GET_TOK(tok, lex);
        DEBUG("tok = " << tok);

        auto innerPs = lex.startSpan();
        bool isShortBind = false;
        bool isBox = false;
        auto bindType = ASTPatternBinding::Type::MOVE;
        bool isMut = false;
        if (tok.type() == TOK_RWORD_BOX) {
            isBox = true;
            isShortBind = true;
            GET_TOK(tok, lex);
        }
        if (tok.type() == TOK_RWORD_REF) {
            isShortBind = true;
            GET_TOK(tok, lex);
            if (tok.type() == TOK_RWORD_MUT) {
                bindType = ASTPatternBinding::Type::MUTREF;
                GET_TOK(tok, lex);
            } else {
                bindType = ASTPatternBinding::Type::REF;
            }
        } else if (tok.type() == TOK_RWORD_MUT) {
            isMut = true;
            isShortBind = true;
            GET_TOK(tok, lex);
        }

        CHECK_TOK(tok, TOK_IDENT);
        auto fieldIdent = tok.ident();
        RcString fieldName;
        GET_TOK(tok, lex);

        ASTPattern pat;
        if (isShortBind || tok.type() != TOK_COLON) {
            PUTBACK(tok, lex);
            pat = ASTPattern(lex.endSpan(innerPs), {});
            fieldName = fieldIdent.name;
            pat.bindings().push_back(ASTPatternBinding(mv$(fieldIdent), bindType, isMut));
            if (isBox) {
                pat = ASTPattern(ASTPattern::TagBox(), lex.endSpan(innerPs), mv$(pat));
            }
        } else {
            CHECK_TOK(tok, TOK_COLON);
            fieldName = mv$(fieldIdent.name);
            pat = ParsePattern(lex);
        }

        subpats.push_back(ASTStructPatternEntry{mv$(attrs), mv$(fieldName), mv$(pat)});
    } while (GET_TOK(tok, lex) == TOK_COMMA);
    CHECK_TOK(tok, TOK_BRACE_CLOSE);

    return ASTPattern(ASTPattern::TagStruct(), lex.endSpan(ps), ::std::move(path), ::std::move(subpats), isExhaustive);
}

#undef NEWNODE

template <typename T>
Spanned<T> getSpanned(TokenStream& lex, ::std::function<T()> f) {
    auto ps = lex.startSpan();
    auto v = f();
    return Spanned<T>{lex.endSpan(ps), mv$(v)};
}

#define GET_SPANNED(type, lex, val) \
    getSpanned<type>(lex, [&]() {   \
        return val;                 \
    })

// Check the next two tokens
#define LOOKAHEAD2(lex, tok1, tok2) ((lex).lookahead(0) == (tok1) && (lex).lookahead(1) == (tok2))

FsPath dirname(::std::string input) {
    while (input.size() > 0 && input.back() != '/' && input.back() != '\\') {
        input.pop_back();
    }
    return input;
}

ASTAttributeList ParseItemAttrs(TokenStream& lex);
void ParseParentAttrs(TokenStream& lex, ASTAttributeList& out);
ASTAttribute ParseMetaItem(TokenStream& lex);
void ParseModRoot(TokenStream& lex, ASTModule& mod, ASTAttributeList& modAttrs);
bool ParseMacroInvocationOpt(TokenStream& lex, ASTMacroInvocation& outInv);

ASTVisibility ParsePublicity(TokenStream& lex, bool allowRestricted /*=true*/) {
    Token tok;
    if (lex.getTokenIf(TOK_INTERPOLATED_VIS, tok)) {
        return tok.takeFragVis();
    }
    if (lex.lookahead(0) == TOK_RWORD_CRATE && lex.lookahead(1) != TOK_DOUBLE_COLON) {
        GET_CHECK_TOK(tok, lex, TOK_RWORD_CRATE);
        return ASTVisibility::makeRestricted(ASTVisibility::Ty::Crate, ASTAbsolutePath("", {}));
    }
    if (lex.getTokenIf(TOK_RWORD_PUB)) {
        if (LOOK_AHEAD(lex) == TOK_PAREN_OPEN) {
            // In tuple fields, `pub (Type,)` must stay distinct from restricted visibility such as `pub(self)`.
            if (!allowRestricted) {
                if (lex.lookahead(1) == TOK_RWORD_IN)
                    ;
                else if (lex.lookahead(1) == TOK_RWORD_CRATE && lex.lookahead(2) == TOK_PAREN_CLOSE)
                    ;
                else if (lex.lookahead(1) == TOK_RWORD_SUPER && lex.lookahead(2) == TOK_PAREN_CLOSE)
                    ;
                else if (lex.lookahead(1) == TOK_RWORD_SELF && lex.lookahead(2) == TOK_PAREN_CLOSE)
                    ;
                else {
                    return ASTVisibility::makeGlobal();
                }
            }
            auto path = ASTAbsolutePath("", {});
            // Restricted publicity.
            GET_TOK(tok, lex); // '('

            switch (GET_TOK(tok, lex)) {
                case TOK_RWORD_CRATE:
                    // Crate visibility
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return ASTVisibility::makeRestricted(ASTVisibility::Ty::PubCrate, std::move(path));
                case TOK_RWORD_SELF:
                    // Private!
                    path = lex.parseState().getCurrentMod().path();
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return ASTVisibility::makeRestricted(ASTVisibility::Ty::PubSelf, std::move(path));
                case TOK_RWORD_SUPER:
                    path = lex.parseState().getCurrentMod().path();
                    path.nodes.pop_back();
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return ASTVisibility::makeRestricted(ASTVisibility::Ty::PubSuper, std::move(path));
                    break;
                case TOK_RWORD_IN: {
                    ASTPath astPath;
                    switch (GET_TOK(tok, lex)) {
                        case TOK_DOUBLE_COLON:
                            astPath = ASTPath("", {});
                            PUTBACK(tok, lex);
                            break;
                        case TOK_IDENT:
                            astPath = ASTPath::newRelative({}, {});
                            astPath.nodes().push_back(tok.ident().name);
                            path.nodes.push_back(tok.ident().name);
                            break;
                        case TOK_RWORD_CRATE:
                            astPath = ASTPath("", {});
                            break;
                        case TOK_RWORD_SELF:
                            astPath = ASTPath::newSelf({});
                            path = lex.parseState().getCurrentMod().path();
                            break;
                        case TOK_RWORD_SUPER:
                            astPath = ASTPath::newSuper(1, {});
                            path = lex.parseState().getCurrentMod().path();
                            path.nodes.pop_back();
                            while (lex.lookahead(0) == TOK_DOUBLE_COLON && lex.lookahead(1) == TOK_RWORD_SUPER) {
                                GET_TOK(tok, lex);
                                GET_TOK(tok, lex);
                                path.nodes.pop_back();
                                astPath.cls.as_Super().count += 1;
                            }
                            break;
                        default:
                            throw ParseErrorUnexpected(lex, tok);
                    }
                    while (lex.getTokenIf(TOK_DOUBLE_COLON)) {
                        GET_CHECK_TOK(tok, lex, TOK_IDENT);
                        path.nodes.push_back(tok.ident().name);
                        astPath.nodes().push_back(tok.ident().name);
                    }
                    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                    return ASTVisibility::makeRestricted(std::move(path), std::move(astPath));
                }
                default:
                    throw ParseErrorUnexpected(lex, tok);
            }
        }
        return ASTVisibility::makeGlobal();
    } else {
        return ASTVisibility::makeRestricted(ASTVisibility::Ty::Private, lex.parseState().getCurrentMod().path());
    }
}

ASTHigherRankedBounds ParseHRB(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    ASTHigherRankedBounds rv;
    GET_CHECK_TOK(tok, lex, TOK_LT);
    do {
        // Support empty lists and comma-terminated lists
        if (lex.lookahead(0) == TOK_GT) {
            GET_TOK(tok, lex);
            break;
        }
        auto attrs = ParseItemAttrs(lex);

        switch (GET_TOK(tok, lex)) {
            case TOK_LIFETIME:
                rv.mLifetimes.push_back(ASTLifetimeParam(lex.pointSpan(), ::std::move(attrs), tok.ident()));
                break;
            case TOK_IDENT:
                // Type parameters in higher-ranked binders are accepted by the
                // parser so macro `ty` fragments can capture them.  They are
                // feature-gated before they can have semantic meaning.
                if (lex.getTokenIf(TOK_EQUAL)) {
                    ParseType(lex);
                }
                break;
            default:
                throw ParseErrorUnexpected(lex, tok, {TOK_LIFETIME, TOK_IDENT});
        }
    } while (GET_TOK(tok, lex) == TOK_COMMA);
    CHECK_TOK(tok, TOK_GT);
    return rv;
}

ASTHigherRankedBounds ParseHRBOpt(TokenStream& lex) {
    if (lex.lookahead(0) == TOK_RWORD_FOR) {
        lex.getToken(); // Consume
        return ParseHRB(lex);
    } else {
        return ASTHigherRankedBounds();
    }
}

namespace {
    ASTLifetimeRef getLifetimeRef(TokenStream& lex, Token tok) {
        CHECK_TOK(tok, TOK_LIFETIME);
        return ASTLifetimeRef(/*lex.point_span(), */ tok.ident());
    }

    ASTBoundConstness ParseBoundConstness(TokenStream& lex) {
        Token tok;
        if (lex.getTokenIf(TOK_TILDE)) {
            GET_CHECK_TOK(tok, lex, TOK_RWORD_CONST);
            return ASTBoundConstness::Maybe;
        }
        if (lex.getTokenIf(TOK_RWORD_CONST)) {
            return ASTBoundConstness::Always;
        }
        if (lex.lookahead(0) == TOK_SQUARE_OPEN && lex.lookahead(1) == TOK_RWORD_CONST && lex.lookahead(2) == TOK_SQUARE_CLOSE) {
            lex.getToken();
            lex.getToken();
            lex.getToken();
            return ASTBoundConstness::Maybe;
        }
        return ASTBoundConstness::Never;
    }
}

/// Parse type parameters in a definition
void ParseTypeBound(TokenStream& lex, ASTGenericParams& ret, ASTType* checkedType, ASTHigherRankedBounds outerHrbs = {}, bool retainBareType = false) {
    TRACE_FUNCTION;
    Token tok;

    // Empty bound list
    if (lex.lookahead(0) == TOK_COMMA || lex.lookahead(0) == TOK_BRACE_OPEN || lex.lookahead(0) == TOK_SEMICOLON) {
        // A `where T:` predicate imposes no bound, but the type must still be
        // retained so that any anon-const side effects it carries are resolved
        // consistently with lowering (which visits every anon module).
        if (retainBareType) {
            ret.mBareBoundTypes.push_back(mv$(checkedType));
        }
        return;
    }

    bool isFirst = true;
    do {
        auto ps = lex.startSpan();
        // If an item terminator is seen (end of item, start of body, list separator), return early.
        if (!isFirst && (LOOK_AHEAD(lex) == TOK_SEMICOLON || LOOK_AHEAD(lex) == TOK_COMMA || LOOK_AHEAD(lex) == TOK_GT)) {
            return;
        }
        isFirst = false;

        if (lex.getTokenIf(TOK_LIFETIME, tok)) {
            ret.addBound(ASTGenericBound::make_TypeLifetime({checkedType->clone(), getLifetimeRef(lex, mv$(tok))}));
        } else if (lex.getTokenIf(TOK_QMARK)) {
            auto hrbs = ParseHRBOpt(lex);
            (void)hrbs; // The only valid ?Trait is Sized, which doesn't have any generics
            ret.addBound(ASTGenericBound::make_MaybeTrait({checkedType->clone(), ParsePath(lex, PATH_GENERIC_TYPE)}));
        } else {
            auto constness = ParseBoundConstness(lex);
            ASTHigherRankedBounds innerHrls;
            if (lex.getTokenIf(TOK_RWORD_FOR)) {
                innerHrls = ParseHRB(lex);
            }
            auto postHrbConstness = ParseBoundConstness(lex);
            if (postHrbConstness != ASTBoundConstness::Never) {
                constness = postHrbConstness;
            }
            auto traitPath = ParsePath(lex, PATH_GENERIC_TYPE);

            auto thisOuterHrbs = (lex.lookahead(0) == TOK_PLUS ? ASTHigherRankedBounds(outerHrbs) : mv$(outerHrbs));
            ret.addBound(ASTGenericBound::make_IsTrait({lex.endSpan(ps), mv$(thisOuterHrbs), checkedType->clone(), mv$(innerHrls), mv$(traitPath), constness}));
        }
    } while (lex.getTokenIf(TOK_PLUS));
}

/// Parse type parameters within '<' and '>' (definition)
ASTGenericParams ParseGenericParams(TokenStream& lex) {
    TRACE_FUNCTION;

    ASTGenericParams ret;
    Token tok;
    do {
        if (GET_TOK(tok, lex) == TOK_GT || tok.type() == TOK_GTE) {
            break;
        }

        PUTBACK(tok, lex);
        auto attrs = ParseItemAttrs(lex);

        GET_TOK(tok, lex);
        if (tok.type() == TOK_IDENT) {
            auto paramName = tok.ident().name;
            auto paramDef = ASTTypeParam(lex.typePool(), lex.pointSpan(), ::std::move(attrs), paramName);

            size_t boundStart = SIZE_MAX;
            size_t boundEnd = SIZE_MAX;
            auto paramTy = mkType(lex.typePool(), lex.pointSpan(), paramName);
            if (GET_TOK(tok, lex) == TOK_COLON) {
                boundStart = ret.bounds.size();
                ParseTypeBound(lex, ret, mv$(paramTy));
                boundEnd = ret.bounds.size();

                GET_TOK(tok, lex);
            }

            if (tok.type() == TOK_EQUAL) {
                paramDef.setDefault(ParseType(lex));
                GET_TOK(tok, lex);
            }
            ret.addTyParam(mv$(paramDef), boundStart, boundEnd);
        } else if (tok.type() == TOK_LIFETIME) {
            size_t boundStart = SIZE_MAX;
            size_t boundEnd = SIZE_MAX;
            auto paramName = tok.ident();
            auto ref = getLifetimeRef(lex, mv$(tok));
            if (GET_TOK(tok, lex) == TOK_COLON) {
                boundStart = ret.bounds.size();
                if (lex.lookahead(0) == TOK_LIFETIME) {
                    do {
                        GET_CHECK_TOK(tok, lex, TOK_LIFETIME);
                        ret.addBound(ASTGenericBound::make_Lifetime({ASTLifetimeRef(ref), getLifetimeRef(lex, mv$(tok))}));
                    } while (GET_TOK(tok, lex) == TOK_PLUS);
                } else {
                    GET_TOK(tok, lex);
                }
                boundEnd = ret.bounds.size();
            }
            ret.addLftParam(ASTLifetimeParam(lex.pointSpan(), ::std::move(attrs), paramName), boundStart, boundEnd);
        } else if (tok.type() == TOK_RWORD_CONST) {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto paramName = tok.ident();
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto ty = ParseType(lex);

            ASTExpr val;
            if (GET_TOK(tok, lex) == TOK_EQUAL) {
                if (lex.lookahead(0) == TOK_BRACE_OPEN) {
                    val = ParseExprBlock(lex);
                } else {
                    val = ParseExprVal(lex);
                }
                GET_TOK(tok, lex);
            }

            ret.addValueParam(lex.pointSpan(), mv$(attrs), mv$(paramName), mv$(ty), mv$(val));
        } else {
            throw ParseErrorUnexpected(lex, tok, {TOK_IDENT, TOK_LIFETIME});
        }
    } while (tok.type() == TOK_COMMA);

    if (tok.type() == TOK_GT) {
    } else if (tok.type() == TOK_GTE) {
        lex.putback(TOK_EQUAL);
    } else {
        throw ParseErrorUnexpected(lex, tok, {TOK_GT, TOK_GTE});
    }
    return ret;
}

ASTGenericParams ParseGenericParamsOpt(TokenStream& lex) {
    if (lex.getTokenIf(TOK_LT)) {
        return ParseGenericParams(lex);
    } else {
        return ASTGenericParams();
    }
}

/// Parse the contents of a 'where' clause
void ParseWhereClause(TokenStream& lex, ASTGenericParams& params) {
    TRACE_FUNCTION;
    Token tok;

    do {
        if (lex.lookahead(0) == TOK_BRACE_OPEN || lex.lookahead(0) == TOK_SEMICOLON) {
            break;
        }

        if (lex.getTokenIf(TOK_LIFETIME, tok)) {
            auto lhs = getLifetimeRef(lex, std::move(tok));
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            do {
                GET_CHECK_TOK(tok, lex, TOK_LIFETIME);
                auto rhs = getLifetimeRef(lex, mv$(tok));
                params.addBound(ASTGenericBound::make_Lifetime({lhs, rhs}));
            } while (lex.getTokenIf(TOK_PLUS));
        }
        // Higher-ranked types/lifetimes
        else if (lex.getTokenIf(TOK_RWORD_FOR)) {
            auto hrbs = ParseHRB(lex);

            ASTType* type = ParseType(lex);
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            ParseTypeBound(lex, params, mv$(type), mv$(hrbs), /*retainBareType=*/true);
        } else {
            ASTType* type = ParseType(lex);
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            ParseTypeBound(lex, params, mv$(type), {}, /*retainBareType=*/true);
        }
    } while (lex.getTokenIf(TOK_COMMA));
}

// Parse a single function argument
ASTFunction::Arg ParseFunctionArg(TokenStream& lex, bool expectNamed) {
    TRACE_FUNCTION_F("expect_named = " << expectNamed);
    Token tok;

    auto attrs = ParseItemAttrs(lex);

    // If any of the following
    // - Expecting a named parameter (i.e. defining a function in root or impl)
    // - Next token is an underscore (only valid as a pattern here)
    // - Next token is 'mut' (a mutable parameter slot) or 'ref' (ref pattern)
    // - Next two are <ident> ':' (a trivial named parameter)
    // NOTE: When not expecting a named param, destructuring patterns are not allowed
    ASTPattern pat;
    if (expectNamed || LOOK_AHEAD(lex) == TOK_UNDERSCORE || LOOK_AHEAD(lex) == TOK_RWORD_REF || LOOK_AHEAD(lex) == TOK_RWORD_MUT || (LOOK_AHEAD(lex) == TOK_IDENT && lex.lookahead(1) == TOK_COLON)) {
        pat = ParsePattern(lex, AllowOrPattern::No);
        GET_CHECK_TOK(tok, lex, TOK_COLON);
    }

    auto ty = ParseType(lex);

    return ASTFunction::Arg(mv$(pat), mv$(ty), mv$(attrs));
}

/// Parse a function definition (after the 'fn <name>')
ASTFunction ParseFunctionDef(TokenStream& lex, bool allowSelf, bool canBePrototype, std::string abi, ASTFunction::Flags flags) {
    TRACE_FUNCTION;
    static const RcString rcstringSelfLower = RcString::newInterned("self");
    static const RcString rcstringSelf = RcString::newInterned("Self");
    ProtoSpan ps = lex.startSpan();

    Token tok;

    // Parameters
    ASTGenericParams params = ParseGenericParamsOpt(lex);

    ASTFunction::Arglist args;

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
            auto ps = lex.startSpan();
            ASTLifetimeRef lifetime;
            if (GET_TOK(tok, lex) == TOK_LIFETIME) {
                lifetime = getLifetimeRef(lex, mv$(tok));
                GET_TOK(tok, lex);
            }

            bool isMut = false;
            if (tok.type() == TOK_RWORD_MUT) {
                isMut = true;
                GET_TOK(tok, lex);
            }
            CHECK_TOK(tok, TOK_RWORD_SELF);
            auto sp = lex.endSpan(ps);
            args.push_back(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), sp, rcstringSelfLower), mkType(lex.typePool(), ASTTypeTags::Reference(), sp, ::std::move(lifetime), isMut, mkType(lex.typePool(), sp, rcstringSelf, 0xFFFF))));
            //if( allow_self == false )

            // Prime tok for next step
            GET_TOK(tok, lex);
        } else {
            // Unbound method
        }
    } else if (tok.type() == TOK_RWORD_MUT) {
        if (LOOK_AHEAD(lex) == TOK_RWORD_SELF) {
            GET_TOK(tok, lex);
            //if( allow_self == false )
            auto bindingSp = lex.endSpan(ps);
            ASTType* ty = mkType(lex.typePool(), lex.pointSpan(), rcstringSelf, 0xFFFF);
            if (GET_TOK(tok, lex) == TOK_COLON) {
                // Typed mut self
                ty = ParseType(lex);
            } else {
                PUTBACK(tok, lex);
            }
            args.push_back(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), bindingSp, rcstringSelfLower), mv$(ty)));
            GET_TOK(tok, lex);
        }
    } else if (tok.type() == TOK_RWORD_SELF) {
        // By-value method
        //if( allow_self == false )
        auto bindingSp = lex.endSpan(ps);
        ASTType* ty = mkType(lex.typePool(), lex.pointSpan(), rcstringSelf, 0xFFFF);
        if (GET_TOK(tok, lex) == TOK_COLON) {
            // Typed mut self
            ty = ParseType(lex);
        } else {
            PUTBACK(tok, lex);
        }
        args.push_back(ASTFunction::Arg(ASTPattern(ASTPattern::TagBind(), bindingSp, rcstringSelfLower), mv$(ty)));
        GET_TOK(tok, lex);
    } else {
        // Unbound method
    }

    // In 2018, patterns must always be provided
    if (lex.editionAfter(ASTEdition::Rust2018)) {
        canBePrototype = false;
    }

    bool isVariadic = false;
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
                isVariadic = true;
                GET_TOK(tok, lex);
                break;
            }
            if ((lex.lookahead(0) == TOK_IDENT || lex.lookahead(0) == TOK_UNDERSCORE) && lex.lookahead(1) == TOK_COLON && lex.lookahead(2) == TOK_TRIPLE_DOT) {
                GET_TOK(tok, lex);
                GET_TOK(tok, lex);
                GET_TOK(tok, lex);
                isVariadic = true;
                GET_TOK(tok, lex);
                break;
            }
            args.push_back(ParseFunctionArg(lex, !canBePrototype));
        } while (GET_TOK(tok, lex) == TOK_COMMA);
        CHECK_TOK(tok, TOK_PAREN_CLOSE);
    } else {
        // Eat 'tok', negative comparison
    }

    // Return type
    ASTType* retType = lex.getTokenIf(TOK_THINARROW) ? ParseType(lex) : mkType(lex.typePool(), ASTTypeTags::Unit(), lex.pointSpan());

    // Bounds
    if (lex.getTokenIf(TOK_RWORD_WHERE)) {
        ParseWhereClause(lex, params);
    }

    return ASTFunction(lex.endSpan(ps), mv$(abi), mv$(flags), mv$(params), mv$(retType), mv$(args), isVariadic);
}

ASTFunction ParseFunctionDefWithCode(TokenStream& lex, bool allowSelf, std::string abi, ASTFunction::Flags flags) {
    Token tok;
    auto ret = ParseFunctionDef(lex, allowSelf, /*can_be_prototype=*/false, std::move(abi), flags);
    GET_TOK(tok, lex);
    if (tok == TOK_BRACE_OPEN) {
    } else if (tok.type() == TOK_INTERPOLATED_BLOCK) {
    } else if (tok.type() == TOK_SEMICOLON) {
        // Used for #[rustc_intrinsic] tagged functions
        return ret;
    } else {
        throw ParseErrorUnexpected(lex, tok, {TOK_BRACE_OPEN, TOK_INTERPOLATED_BLOCK});
    }
    // Enter a new hygine scope for the function (TODO: Should this be in Parse_ExprBlock?)
    lex.pushHygine();
    PUTBACK(tok, lex);
    ret.setCode(ParseExprBlock(lex));
    lex.popHygine();
    return ret;
}

ASTTypeAlias ParseTypeAlias(TokenStream& lex) {
    TRACE_FUNCTION;

    Token tok;

    // Params
    ASTGenericParams params = ParseGenericParamsOpt(lex);

    GET_TOK(tok, lex);
    if (tok.type() == TOK_RWORD_WHERE) {
        ParseWhereClause(lex, params);
        GET_TOK(tok, lex);
    }
    CHECK_TOK(tok, TOK_EQUAL);

    // Type
    ASTType* type = ParseType(lex);
    GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

    return ASTTypeAlias(::std::move(params), ::std::move(type));
}

ASTStruct ParseStruct(TokenStream& lex, const ASTAttributeList& metaItems) {
    TRACE_FUNCTION;

    Token tok;

    tok = lex.getToken();
    ASTGenericParams params;
    if (tok.type() == TOK_LT) {
        params = ParseGenericParams(lex);
        tok = lex.getToken();
    }

    if (tok.type() == TOK_PAREN_OPEN) {
        // Tuple structs
        ::std::vector<ASTTupleItem> refs;
        while (!lex.getTokenIf(TOK_PAREN_CLOSE)) {
            auto itemAttrs = ParseItemAttrs(lex);
            SET_ATTRS(lex, itemAttrs);

            auto vis = ParsePublicity(lex, /*allow_restricted=*/false); // Disambiguate `pub (Type)` from tuple-field restricted visibility.

            refs.push_back(ASTTupleItem(mv$(itemAttrs), vis, ParseType(lex)));
            if (GET_TOK(tok, lex) != TOK_COMMA) {
                CHECK_TOK(tok, TOK_PAREN_CLOSE);
                break;
            }
        }

        if (lex.getTokenIf(TOK_RWORD_WHERE)) {
            ParseWhereClause(lex, params);
        }
        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
        //if( refs.size() == 0 )
        return ASTStruct(mv$(params), mv$(refs));
    } else {
        // Unit-like struct
        if (tok.type() == TOK_RWORD_WHERE) {
            ParseWhereClause(lex, params);
            tok = lex.getToken();
        }

        if (tok.type() == TOK_SEMICOLON) {
            CHECK_TOK(tok, TOK_SEMICOLON);
            return ASTStruct(mv$(params));
        } else if (tok.type() == TOK_BRACE_OPEN) {
            ::std::vector<ASTStructItem> items;
            while (!lex.getTokenIf(TOK_BRACE_CLOSE)) {
                auto itemAttrs = ParseItemAttrs(lex);
                SET_ATTRS(lex, itemAttrs);

                auto vis = ParsePublicity(lex);

                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                auto name = tok.ident().name;
                GET_CHECK_TOK(tok, lex, TOK_COLON);
                ASTType* type = ParseType(lex);
                ASTExpr defaultValue = lex.getTokenIf(TOK_EQUAL) ? ParseExpr(lex) : ASTExpr();

                items.push_back(ASTStructItem(mv$(itemAttrs), vis, mv$(name), mv$(type), std::move(defaultValue)));
                if (GET_TOK(tok, lex) == TOK_BRACE_CLOSE) {
                    break;
                }
                CHECK_TOK(tok, TOK_COMMA);
            }
            //if( items.size() == 0 )
            return ASTStruct(mv$(params), mv$(items));
        } else {
            throw ParseErrorUnexpected(lex, tok);
        }
    }
}

static void ParseFunctionQualifiers(TokenStream& lex, Token& tok, ASTFunction::Flags& flags, ::std::string& abi) {
    if (tok.type() == TOK_RWORD_CONST) {
        switch (lex.lookahead(0)) {
            case TOK_RWORD_ASYNC:
            case TOK_RWORD_UNSAFE:
            case TOK_RWORD_EXTERN:
            case TOK_RWORD_FN:
                break;
            default:
                return;
        }
    }

    for (;;) {
        switch (tok.type()) {
            case TOK_RWORD_CONST:
                flags.isConst = true;
                GET_TOK(tok, lex);
                break;
            case TOK_RWORD_ASYNC:
                flags.isAsync = true;
                GET_TOK(tok, lex);
                break;
            case TOK_RWORD_UNSAFE:
                flags.isUnsafe = true;
                GET_TOK(tok, lex);
                break;
            case TOK_RWORD_EXTERN:
                if (GET_TOK(tok, lex) == TOK_STRING) {
                    abi = tok.str();
                    GET_TOK(tok, lex);
                } else {
                    abi = "C";
                }
                break;
            default:
                return;
        }
    }
}

ASTNamed<ASTItem> ParseTraitItem(TokenStream& lex) {
    Token tok;

    auto itemAttrs = ParseItemAttrs(lex);
    SET_ATTRS(lex, itemAttrs);

    auto ps = lex.startSpan();

    {
        ASTMacroInvocation inv;
        if (ParseMacroInvocationOpt(lex, inv)) {
            return ASTNamed<ASTItem>{lex.endSpan(ps), mv$(itemAttrs), ASTVisibility::makeGlobal(), "", ASTItem(mv$(inv))};
        }
    }

    // An already-parsed `$item:item` fragment. A trait item *is* an `AST::Named<AST::Item>`, so it is handed straight back.
    if (lex.lookahead(0) == TOK_INTERPOLATED_ITEM) {
        tok = lex.getToken();
        auto item = tok.takeFragItem();
        for (auto& a : itemAttrs.mItems) {
            item.attrs.mItems.push_back(std::move(a));
        }
        // Only the kinds a trait body can hold; anything else is a loud TODO rather than silently accepted.
        TU_MATCH_HDRA((item.data), {)
        default:
            TODO(lex.pointSpan(), "Interpolated item into trait: " << item.data.tagStr());
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
    bool isSpecialisable = false;
    if (tok.type() == TOK_IDENT && tok.ident().name == "default") {
        isSpecialisable = true;
        GET_TOK(tok, lex);
    }
    // TODO: Mark specialisation
    (void)isSpecialisable;

    std::string abi = ABI_RUST;
    ASTFunction::Flags fnFlags;
    ParseFunctionQualifiers(lex, tok, fnFlags, abi);

    RcString name;
    ASTItem rv;
    switch (tok.type()) {
        case TOK_RWORD_STATIC: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            name = tok.ident().name;
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto ty = ParseType(lex);
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

            ASTExpr val;
            if (GET_TOK(tok, lex) == TOK_EQUAL) {
                val = ParseExpr(lex);
                GET_TOK(tok, lex);
            }
            CHECK_TOK(tok, TOK_SEMICOLON);

            rv = ASTStatic(ASTStatic::STATIC, mv$(ty), val);
            break;
        }
        case TOK_RWORD_CONST: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            name = tok.ident().name;
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto ty = ParseType(lex);

            ASTExpr val;
            if (GET_TOK(tok, lex) == TOK_EQUAL) {
                val = ParseExpr(lex);
                GET_TOK(tok, lex);
            }
            CHECK_TOK(tok, TOK_SEMICOLON);

            rv = ASTStatic(ASTStatic::CONST, mv$(ty), val);
            break;
        }
        // Associated type
        case TOK_RWORD_TYPE: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            name = tok.ident().name;
            auto typeParams = ParseGenericParamsOpt(lex);
            ASTGenericParams bounds;
            if (GET_TOK(tok, lex) == TOK_COLON) {
                // Bounded associated type
                ParseTypeBound(lex, bounds, mkType(lex.typePool(), lex.pointSpan(), RcString::newInterned("Self"), 0xFFFF));
                GET_TOK(tok, lex);
            }

            ASTType* defaultType = mkType(lex.typePool(), lex.pointSpan());
            if (tok.type() == TOK_EQUAL) {
                defaultType = ParseType(lex);
                GET_TOK(tok, lex);
            }
            if (tok.type() == TOK_RWORD_WHERE) {
                ParseWhereClause(lex, typeParams);
                GET_TOK(tok, lex);
            }

            CHECK_TOK(tok, TOK_SEMICOLON);
            rv = ASTTypeAlias::newAssociatedType(mv$(typeParams), mv$(bounds), mv$(defaultType));
            break;
        }

        // Functions (possibly unsafe, async, or extern)
        case TOK_RWORD_FN: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            name = tok.ident().name;
            // Self allowed, prototype-form allowed (optional names and no code)
            auto fcn = ParseFunctionDef(lex, /*allow_self*/ true, /*can_be_proto*/ true, std::move(abi), fnFlags);
            if (lex.lookahead(0) == TOK_BRACE_OPEN) {
                // Enter a new hygine scope for the function body. (TODO: Should this be in Parse_ExprBlock?)
                lex.pushHygine();
                fcn.setCode(ParseExprBlock(lex));
                lex.popHygine();
            } else if (lex.getTokenIf(TOK_SEMICOLON)) {
                // Accept it
            } else {
                throw ParseErrorUnexpected(lex, tok);
            }
            rv = ::std::move(fcn);
            break;
        }
        case TOK_IDENT:
            if (tok.ident().name == "reuse") {
                auto fcn = ParseDelegationFunction(lex, name);
                rv = mv$(fcn);
                break;
            }
            throw ParseErrorUnexpected(lex, tok);
        default:
            throw ParseErrorUnexpected(lex, tok);
    }

    return ASTNamed<ASTItem>(lex.endSpan(ps), mv$(itemAttrs), ASTVisibility::makeGlobal(), mv$(name), mv$(rv));
}

ASTTrait ParseTraitDef(TokenStream& lex, const ASTAttributeList& metaItems, ASTGenericParams params) {
    TRACE_FUNCTION;

    Token tok;

    GET_TOK(tok, lex);

    // Trait bounds "trait Trait : 'lifetime + OtherTrait + OtherTrait2"
    ::std::vector<Spanned<TypeTraitPath>> supertraits;
    ::std::vector<Spanned<ASTLifetimeRef>> lifetimes;
    if (tok.type() == TOK_COLON) {
        // TODO: Just add these as `where Self: <foo>` (would that break typecheck?)
        do {
            if (GET_TOK(tok, lex) == TOK_LIFETIME) {
                lifetimes.push_back(GET_SPANNED(ASTLifetimeRef, lex, ASTLifetimeRef(tok.ident())));
            } else if (tok.type() == TOK_BRACE_OPEN) {
                break;
            } else {
                PUTBACK(tok, lex);
                auto constness = ParseBoundConstness(lex);
                auto hrbs = ParseHRBOpt(lex);
                auto postHrbConstness = ParseBoundConstness(lex);
                if (postHrbConstness != ASTBoundConstness::Never) {
                    constness = postHrbConstness;
                }
                supertraits.push_back(GET_SPANNED(TypeTraitPath, lex, (TypeTraitPath(mv$(hrbs), ParsePath(lex, PATH_GENERIC_TYPE), constness))));
            }
        } while (GET_TOK(tok, lex) == TOK_PLUS);
    }

    if (tok.type() == TOK_RWORD_WHERE) {
        //if( params.ty_params().size() == 0 )
        ParseWhereClause(lex, params);
        tok = lex.getToken();
    }

    ASTTrait trait(mv$(params), mv$(supertraits), mv$(lifetimes));

    CHECK_TOK(tok, TOK_BRACE_OPEN);
    while (GET_TOK(tok, lex) != TOK_BRACE_CLOSE) {
        PUTBACK(tok, lex);

        auto item = ParseTraitItem(lex);
        if (item.data.is_Function() && item.data.as_Function().delegation()
            && item.data.as_Function().delegation()->targets.size() > 1) {
            for (auto& split : SplitDelegationFunction(item.data.as_Function())) {
                trait.items().push_back(ASTNamed<ASTItem>{item.span, item.attrs.clone(), item.vis, mv$(split.first), ASTItem(mv$(split.second))});
            }
        } else {
            trait.items().push_back(mv$(item));
        }
    }

    return trait;
}

ASTEnum ParseEnumDef(TokenStream& lex, const ASTAttributeList& metaItems) {
    TRACE_FUNCTION;

    Token tok;

    tok = lex.getToken();
    // Type params supporting "where"
    ASTGenericParams params;
    if (tok.type() == TOK_LT) {
        params = ParseGenericParams(lex);
        if (GET_TOK(tok, lex) == TOK_RWORD_WHERE) {
            ParseWhereClause(lex, params);
            tok = lex.getToken();
        }
    }

    // Body
    CHECK_TOK(tok, TOK_BRACE_OPEN);
    ::std::vector<ASTEnumVariant> variants;
    while (lex.lookahead(0) != TOK_BRACE_CLOSE) {
        auto sp = lex.startSpan();

        auto itemAttrs = ParseItemAttrs(lex);
        SET_ATTRS(lex, itemAttrs);

        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto name = tok.ident().name;
        // Tuple-like variants
        if (lex.getTokenIf(TOK_PAREN_OPEN)) {
            ::std::vector<ASTTupleItem> items;
            // Get type list
            do {
                if (lex.getTokenIf(TOK_PAREN_CLOSE, tok)) {
                    break;
                }

                auto fieldAttrs = ParseItemAttrs(lex);
                auto ty = ParseType(lex);
                items.emplace_back(std::move(fieldAttrs), ASTVisibility::makeGlobal(), std::move(ty));
            } while (GET_TOK(tok, lex) == TOK_COMMA);
            CHECK_TOK(tok, TOK_PAREN_CLOSE);
            variants.push_back(ASTEnumVariant(mv$(itemAttrs), mv$(name), mv$(items)));
        }
        // Struct-like variants
        else if (lex.getTokenIf(TOK_BRACE_OPEN)) {
            ::std::vector<ASTStructItem> fields;
            do {
                if (lex.getTokenIf(TOK_BRACE_CLOSE, tok)) {
                    break;
                }

                auto fieldAttrs = ParseItemAttrs(lex);

                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                auto name = tok.ident().name;
                GET_CHECK_TOK(tok, lex, TOK_COLON);
                auto ty = ParseType(lex);
                auto def = lex.getTokenIf(TOK_EQUAL) ? ParseExpr(lex) : ASTExpr();
                fields.push_back(ASTStructItem(mv$(fieldAttrs), ASTVisibility::makeGlobal(), mv$(name), mv$(ty), mv$(def)));
            } while (GET_TOK(tok, lex) == TOK_COMMA);
            CHECK_TOK(tok, TOK_BRACE_CLOSE);

            variants.push_back(ASTEnumVariant(mv$(itemAttrs), mv$(name), mv$(fields)));
        }
        // Unit variants
        else {
            variants.push_back(ASTEnumVariant(mv$(itemAttrs), mv$(name)));
        }

        if (lex.getTokenIf(TOK_EQUAL)) {
            variants.back().discriminantValue = ParseExpr(lex);
        }

        if (!lex.getTokenIf(TOK_COMMA)) {
            break;
        }
        // Consumed the comma
    }
    GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);

    return ASTEnum(mv$(params), mv$(variants));
}

ASTUnion ParseUnion(TokenStream& lex, ASTAttributeList& metaItems) {
    Token tok;

    TRACE_FUNCTION;

    ASTGenericParams params;
    if (GET_TOK(tok, lex) == TOK_LT) {
        params = ParseGenericParams(lex);
        if (GET_TOK(tok, lex) == TOK_RWORD_WHERE) {
            ParseWhereClause(lex, params);
            tok = lex.getToken();
        }
    }

    ::std::vector<ASTStructItem> variants;

    CHECK_TOK(tok, TOK_BRACE_OPEN);
    do {
        if (LOOK_AHEAD(lex) == TOK_BRACE_CLOSE) {
            GET_TOK(tok, lex);
            break;
        }

        auto itemAttrs = ParseItemAttrs(lex);
        SET_ATTRS(lex, itemAttrs);

        auto vis = ParsePublicity(lex);

        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto name = tok.ident().name;
        GET_CHECK_TOK(tok, lex, TOK_COLON);

        auto ty = ParseType(lex);

        variants.push_back(ASTStructItem(mv$(itemAttrs), mv$(vis), mv$(name), mv$(ty), {}));

    } while (GET_TOK(tok, lex) == TOK_COMMA);
    CHECK_TOK(tok, TOK_BRACE_CLOSE);

    return ASTUnion(mv$(params), mv$(variants));
}

ASTAttributeList ParseItemAttrs(TokenStream& lex) {
    ASTAttributeList rv;
    Token tok;
    while (lex.lookahead(0) == TOK_HASH) {
        GET_CHECK_TOK(tok, lex, TOK_HASH);
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_OPEN);
        rv.push_back(ParseMetaItem(lex));
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
    }
    return rv;
}

void ParseParentAttrs(TokenStream& lex, ASTAttributeList& out) {
    Token tok;
    while (lex.lookahead(0) == TOK_HASH && lex.lookahead(1) == TOK_EXCLAM) {
        GET_CHECK_TOK(tok, lex, TOK_HASH);
        GET_CHECK_TOK(tok, lex, TOK_EXCLAM);
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_OPEN);
        out.push_back(ParseMetaItem(lex));
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
    }
}

namespace {
    RcString getTokIdentRword(TokenStream& lex) {
        Token tok;
        GET_TOK(tok, lex);
        if (tok.type() == TOK_IDENT) {
            return tok.ident().name;
        }
        if (Token::typeIsRword(tok.type())) {
            return tok.toStr().c_str();
        }
        throw ParseErrorUnexpected(lex, tok, TOK_IDENT);
    }
}

/// Parse a meta-item declaration (either #![ or #[)
ASTAttribute ParseMetaItem(TokenStream& lex) {
    TRACE_FUNCTION;
    Token tok;

    if (lex.lookahead(0) == TOK_INTERPOLATED_META) {
        GET_TOK(tok, lex);
        return mv$(tok.fragMeta());
    }

    auto ps = lex.startSpan();

    ASTAttributeName name;
    // NOTE: After 1.19 mode, values can be present with no name
    if (lex.lookahead(0) != TOK_IDENT && lex.lookahead(0) != TOK_DOUBLE_COLON && !Token::typeIsRword(lex.lookahead(0))) {
        // Put a fake equals token in the queue
        tok = Token(TOK_EQUAL);
    } else {
        name.hasLeading = lex.getTokenIf(TOK_DOUBLE_COLON);
        do {
            name.elems.push_back(getTokIdentRword(lex));
        } while (GET_TOK(tok, lex) == TOK_DOUBLE_COLON);
    }
    DEBUG("name = " << name);
    TokenTree attrData;
    switch (tok.type()) {
        case TOK_EQUAL: {
            std::vector<TokenTree> tt;
            tt.push_back(std::move(tok));
            // - Square close (top-level) AND paren close (cfg_attr)
            while (lex.lookahead(0) != TOK_EOF && lex.lookahead(0) != TOK_SQUARE_CLOSE && lex.lookahead(0) != TOK_PAREN_CLOSE && lex.lookahead(0) != TOK_BRACE_CLOSE && lex.lookahead(0) != TOK_COMMA && lex.lookahead(0) != TOK_SEMICOLON) {
                tt.push_back(ParseTT(lex, false));
            }
            attrData = TokenTree(lex.getEdition(), lex.getHygiene(), std::move(tt));
        } break;
        case TOK_PAREN_OPEN:
        case TOK_SQUARE_OPEN: // 1.74 - openssl v0.10.57
            PUTBACK(tok, lex);
            attrData = ParseTT(lex, false);
            break;
        default:
            // Empty
            PUTBACK(tok, lex);
            break;
    }
    return ASTAttribute(lex.endSpan(ps), name, mv$(attrData));
}

ASTItem ParseImpl(TokenStream& lex, ASTAttributeList& attrs, bool isUnsafe = false) {
    TRACE_FUNCTION;
    Token tok;
    auto ps = lex.startSpan();

    ASTGenericParams params;
    // 1. (optional) type parameters
    if (lex.getTokenIf(TOK_LT)) {
        params = ParseGenericParams(lex);
    }
    // 2. Either a trait name (with type params), or the type to impl

    Spanned<ASTPath> traitPath;

    const bool isConst = lex.getTokenIf(TOK_RWORD_CONST);

    // - Handle negative impls specially, which must be a trait
    // "impl !Trait for Type {}"
    // NOTE: Special case to handle `impl ! {}` (used for docs in 1.90)
    if (GET_TOK(tok, lex) == TOK_EXCLAM && lex.lookahead(0) != TOK_BRACE_OPEN) {
        traitPath = GET_SPANNED(ASTPath, lex, ParsePath(lex, PATH_GENERIC_TYPE));
        GET_CHECK_TOK(tok, lex, TOK_RWORD_FOR);
        auto implType = ParseType(lex, true);

        if (GET_TOK(tok, lex) == TOK_RWORD_WHERE) {
            ParseWhereClause(lex, params);
            GET_TOK(tok, lex);
        }
        CHECK_TOK(tok, TOK_BRACE_OPEN);
        // negative impls can't have any content
        GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);

        return ASTItem::make_NegImpl(ASTImplDef(mv$(params), mv$(traitPath), mv$(implType)));
    }

    // - Don't care which at this stage
    PUTBACK(tok, lex);

    auto implType = ParseType(lex, true);

    if (GET_TOK(tok, lex) == TOK_RWORD_FOR) {
        // Trickery! All traits parse as valid types, so this works.
        if (!implType->isPath()) {
            throw CompileErrorGeneric(lex, "Trait was not a path");
        }
        traitPath = Spanned<ASTPath>{implType->span(), mv$(implType->path())};
        // Implementing a trait for another type, get the target type
        if (GET_TOK(tok, lex) == TOK_DOUBLE_DOT) {
            // Default impl
            implType = mkType(lex.typePool(), ASTTypeTags::Invalid(), lex.pointSpan());
        } else {
            PUTBACK(tok, lex);
            implType = ParseType(lex, true);
        }
    } else {
        PUTBACK(tok, lex);
    }

    // Where clause
    if (GET_TOK(tok, lex) == TOK_RWORD_WHERE) {
        ParseWhereClause(lex, params);
    } else {
        PUTBACK(tok, lex);
    }
    GET_CHECK_TOK(tok, lex, TOK_BRACE_OPEN);

    ParseParentAttrs(lex, attrs);

    auto impl = ASTImpl(ASTImplDef(mv$(params), mv$(traitPath), mv$(implType)));
    if (isConst) {
        impl.def().setIsConst();
    }

    // A sequence of method implementations
    while (lex.lookahead(0) != TOK_BRACE_CLOSE) {
        ParseImplItem(lex, impl);
    }
    GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);

    return ASTItem::make_Impl(mv$(impl));
}

void ParseImplItem(TokenStream& lex, ASTImpl& impl) {
    TRACE_FUNCTION;
    Token tok;

    auto itemAttrs = ParseItemAttrs(lex);
    SET_ATTRS(lex, itemAttrs);

    {
        ASTMacroInvocation inv;
        if (ParseMacroInvocationOpt(lex, inv)) {
            impl.addMacroInvocation(mv$(inv));
            impl.items().back().attrs = mv$(itemAttrs);
            return;
        }
    }
    {
        if (lex.lookahead(0) == TOK_INTERPOLATED_ITEM) {
            tok = lex.getToken();
            auto item = tok.takeFragItem();
            // Attributes are parsed before the fragment is seen, so without this transfer they are dropped - turning a `#[cfg]` that should remove the item into a no-op.
            for (auto& a : itemAttrs.mItems) {
                item.attrs.mItems.push_back(std::move(a));
            }
            TU_MATCH_HDRA((item.data), {)
            default:
                TODO(lex.pointSpan(), "Interpolated item into impl: " << item.data.tagStr());
                TU_ARMA(Function, e) {
                    impl.addFunction(item.span, std::move(item.attrs), item.vis, false, item.name, std::move(e));
                }
                // An associated `const` - the only kind of `Static` an impl block can hold, stored as the non-interpolated path stores one.
                TU_ARMA(Static, e) {
                    impl.addStatic(item.span, std::move(item.attrs), item.vis, false, item.name, std::move(e));
                }
                //    }
            }
            return ;
        }
    }

    auto ps = lex.startSpan();

    auto vis = ParsePublicity(lex);
    GET_TOK(tok, lex);

    bool isSpecialisable = false;
    if (tok.type() == TOK_IDENT && tok.ident().name == "default") {
        isSpecialisable = true;
        GET_TOK(tok, lex);
    }

    if (tok.type() == TOK_IDENT && tok.ident().name == "reuse") {
        RcString name;
        auto fcn = ParseDelegationFunction(lex, name);
        auto span = lex.endSpan(ps);
        if (fcn.delegation()->targets.size() > 1) {
            for (auto& split : SplitDelegationFunction(fcn)) {
                impl.addFunction(span, itemAttrs.clone(), vis, isSpecialisable, mv$(split.first), mv$(split.second));
            }
        } else {
            impl.addFunction(span, mv$(itemAttrs), vis, isSpecialisable, mv$(name), mv$(fcn));
        }
        return;
    }

    ::std::string abi = ABI_RUST;
    ASTFunction::Flags fnFlags;
    if (tok.type() == TOK_RWORD_TYPE) {
        GET_CHECK_TOK(tok, lex, TOK_IDENT);
        auto name = tok.ident().name;
        auto atypeParams = ParseGenericParamsOpt(lex);
        GET_CHECK_TOK(tok, lex, TOK_EQUAL);
        auto ty = ParseType(lex);
        if (lex.getTokenIf(TOK_RWORD_WHERE)) {
            ParseWhereClause(lex, atypeParams);
        }
        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
        impl.addType(lex.endSpan(ps), mv$(itemAttrs), vis, isSpecialisable, name, mv$(atypeParams), mv$(ty));
        return;
    }

    ParseFunctionQualifiers(lex, tok, fnFlags, abi);
    if (tok.type() == TOK_RWORD_CONST) {
        GET_TOK(tok, lex);
        CHECK_TOK(tok, TOK_IDENT);
        auto name = tok.ident().name;
        GET_CHECK_TOK(tok, lex, TOK_COLON);
        auto ty = ParseType(lex);
        GET_CHECK_TOK(tok, lex, TOK_EQUAL);
        auto val = ParseExpr(lex);
        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

        auto i = ASTStatic(ASTStatic::CONST, mv$(ty), mv$(val));
        impl.addStatic(lex.endSpan(ps), mv$(itemAttrs), vis, isSpecialisable, mv$(name), mv$(i));
        return;
    }
    CHECK_TOK(tok, TOK_RWORD_FN);
    GET_CHECK_TOK(tok, lex, TOK_IDENT);
    // TODO: Hygine on function names? - Not in impl blocks?
    auto name = tok.ident().name;
    DEBUG("Function " << name);
    // - Self allowed, can't be prototype-form
    auto fcn = ParseFunctionDefWithCode(lex, /*allow_self=*/true, std::move(abi), fnFlags);
    impl.addFunction(lex.endSpan(ps), mv$(itemAttrs), vis, isSpecialisable, mv$(name), mv$(fcn));
}

ASTNamed<ASTItem> ParseExternBlockItem(TokenStream& lex, const std::string& abi) {
    Token tok;
    auto metaItems = ParseItemAttrs(lex);
    SET_ATTRS(lex, metaItems);

    auto ps = lex.startSpan();

    {
        ASTMacroInvocation inv;
        if (ParseMacroInvocationOpt(lex, inv)) {
            return ASTNamed<ASTItem>{lex.endSpan(ps), mv$(metaItems), ASTVisibility::makeGlobal(), "", ASTItem(mv$(inv))};
        }
    }

    auto vis = ParsePublicity(lex);
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
            auto i = ASTItem(ParseFunctionDef(lex, /*allow_self*/ false, /*can_be_prototype=*/true, abi, ASTFunction::Flags::makeUnsafe()));
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

            return ASTNamed<ASTItem>{lex.endSpan(ps), mv$(metaItems), vis, mv$(name), mv$(i)};
            break;
        }
        case TOK_RWORD_STATIC: {
            bool isMut = false;
            if (GET_TOK(tok, lex) == TOK_RWORD_MUT) {
                isMut = true;
            } else {
                PUTBACK(tok, lex);
            }
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto name = tok.ident().name;
            GET_CHECK_TOK(tok, lex, TOK_COLON);
            auto type = ParseType(lex);
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

            auto i = ASTItem(ASTStatic((isMut ? ASTStatic::MUT : ASTStatic::STATIC), mv$(type), ASTExpr()));
            return ASTNamed<ASTItem>{lex.endSpan(ps), mv$(metaItems), vis, mv$(name), mv$(i)};
            break;
        }
        case TOK_RWORD_TYPE: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto name = tok.ident().name;
            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
            auto sp = lex.endSpan(ps);
            //TODO(sp, "Extern type");
            auto i = ASTItem(ASTTypeAlias(ASTGenericParams(), ::mkType(lex.typePool(), sp)));
            return ASTNamed<ASTItem>{mv$(sp), mv$(metaItems), vis, mv$(name), mv$(i)};
            break;
        }
        default:
            throw ParseErrorUnexpected(lex, tok, {TOK_RWORD_FN, TOK_RWORD_STATIC, TOK_RWORD_TYPE});
    }
}

ASTExternBlock ParseExternBlock(TokenStream& lex, ::std::string abi, ASTAttributeList& blockAttrs) {
    TRACE_FUNCTION;
    Token tok;

    ParseParentAttrs(lex, blockAttrs);

    ASTExternBlock rv{abi};

    while (GET_TOK(tok, lex) != TOK_BRACE_CLOSE) {
        PUTBACK(tok, lex);

        rv.addItem(ParseExternBlockItem(lex, abi));
    }

    return rv;
}

RcString getOptionalIdent(TokenStream& lex) {
    Token tok;
    GET_TOK(tok, lex);
    if (tok.type() == TOK_UNDERSCORE) {
        static unsigned anonIndex = 0;
        return RcString::newInterned(FMT(" " << anonIndex++));
    } else if (tok.type() == TOK_IDENT) {
        return tok.ident().name;
    } else {
        throw ParseErrorUnexpected(lex, tok, {TOK_UNDERSCORE, TOK_IDENT});
    }
}

/// Parse multiple items from a use "statement"
void ParseUseInner(TokenStream& lex, ::std::vector<ASTUseItem::Ent>& entries, ASTPath& path, bool explicitAbsolute = false) {
    TRACE_FUNCTION_FR(path, entries);
    Token tok;

    while (lex.getTokenIf(TOK_RWORD_SUPER)) {
        lex.getTokenCheck(TOK_DOUBLE_COLON);
        if (auto* p = path.cls.opt_Super()) {
            if (p->nodes.empty()) {
                p->count += 1;
            } else {
                p->nodes.pop_back();
            }
        } else {
            ASSERT_BUG(lex.pointSpan(), path.nodes().size() > 0, "super in empty path");
            path.nodes().pop_back();
        }
    }

    do {
        switch (GET_TOK(tok, lex)) {
            case TOK_IDENT:
                if (explicitAbsolute && lex.editionAfter(ASTEdition::Rust2018) && path.cls.is_Absolute()
                    && path.cls.as_Absolute().crate == "" && path.cls.as_Absolute().nodes.empty()) {
                    path = ASTPath(RcString(std::string("=") + tok.ident().name.c_str()), {});
                } else {
                    path.append(ASTPathNode(tok.ident().name, {}));
                }
                break;
            case TOK_RWORD_SELF: {
                ASSERT_BUG(lex.pointSpan(), !path.nodes().empty(), "`self` with no path");
                auto name = path.nodes().back().name();
                if (lex.getTokenIf(TOK_RWORD_AS)) {
                    name = getOptionalIdent(lex);
                }
                entries.push_back({lex.pointSpan(), ASTPath(path), mv$(name)});
                return;
            }
            case TOK_BRACE_OPEN:
                if (LOOK_AHEAD(lex) == TOK_BRACE_CLOSE) {
                    GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);
                    return;
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
                            name = getOptionalIdent(lex);
                        } else {
                            if (path.nodes().size() == 0) {
                                ERROR(lex.pointSpan(), E0000, "`self` with no path, use `as` to give it a name");
                            }
                            name = path.nodes().back().name();
                        }
                        entries.push_back({lex.pointSpan(), ASTPath(path), ::std::move(name)});
                    } else {
                        auto savedPath = ASTPath(path);

                        ParseUseInner(lex, entries, path, explicitAbsolute);

                        path = std::move(savedPath);
                    }
                } while (GET_TOK(tok, lex) == TOK_COMMA);
                CHECK_TOK(tok, TOK_BRACE_CLOSE);
                return;
            case TOK_DOUBLE_COLON: {
                auto absolutePath = ASTPath("", {});
                ParseUseInner(lex, entries, absolutePath, true);
                return;
            }
            case TOK_STAR:
                entries.push_back({lex.pointSpan(), ASTPath(path), ""});
                return;
            default:
                throw ParseErrorUnexpected(lex, tok);
        }
    } while (GET_TOK(tok, lex) == TOK_DOUBLE_COLON);

    RcString name;

    // NOTE: The above loop has to run once, so the last token HAS to have been an ident
    if (tok.type() == TOK_RWORD_AS) {
        name = getOptionalIdent(lex);
    } else {
        PUTBACK(tok, lex);
        ASSERT_BUG(lex.pointSpan(), path.nodes().size() > 0, "`use` with no path");
        name = path.nodes().back().name();
    }

    // TODO: Get a span covering the final node.
    entries.push_back({lex.pointSpan(), ASTPath(path), ::std::move(name)});
}

void ParseUseRoot(TokenStream& lex, ::std::vector<ASTUseItem::Ent>& entries) {
    ASTPath path = ASTPath("", {});
    bool explicitAbsolute = false;
    Token tok;
    switch (GET_TOK(tok, lex)) {
        case TOK_RWORD_SELF:
            path = ASTPath::newSelf({}); // relative path
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            break;
        case TOK_RWORD_SUPER: {
            unsigned int count = 1;
            while (LOOK_AHEAD(lex) == TOK_DOUBLE_COLON && lex.lookahead(1) == TOK_RWORD_SUPER) {
                GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
                GET_CHECK_TOK(tok, lex, TOK_RWORD_SUPER);
                count += 1;
            }
            path = ASTPath::newSuper(count, {});
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            break;
        }
        case TOK_RWORD_CRATE:
            if (lex.lookahead(0) == TOK_RWORD_AS) {
                GET_CHECK_TOK(tok, lex, TOK_RWORD_AS);
                auto name = getOptionalIdent(lex);
                entries.push_back({lex.pointSpan(), ASTPath(path), ::std::move(name)});
                return;
            }
            // 1.29 absolute path
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            break;
            // Leading :: is allowed and ignored for the $crate feature
        case TOK_DOUBLE_COLON:
            // Absolute path
            explicitAbsolute = true;
            // Internal `$crate` path encoding emitted by mrustc is `::"crate-name"`.
            if (LOOK_AHEAD(lex) == TOK_STRING) {
                GET_CHECK_TOK(tok, lex, TOK_STRING);
                path = ASTPath(RcString::newInterned(tok.str()), {});
                explicitAbsolute = false;
            } else if (lex.editionAfter(ASTEdition::Rust2018) && LOOK_AHEAD(lex) == TOK_IDENT) {
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                // Internal AST encoding: `=crate` denotes a Rust 2018 extern-prelude absolute path.
                path = ASTPath(RcString(std::string("=") + tok.ident().name.c_str()), {});
                explicitAbsolute = false;
                // TODO: Is `use ::foo as bar` valid?
                if (lex.lookahead(0) != TOK_DOUBLE_COLON) {
                    RcString name;
                    if (lex.lookahead(0) == TOK_RWORD_AS) {
                        GET_CHECK_TOK(tok, lex, TOK_RWORD_AS);
                        name = getOptionalIdent(lex);
                    } else {
                        name = path.cls.as_Absolute().crate.c_str() + 1;
                    }

                    // TODO: Get a span covering the final node.
                    entries.push_back({lex.pointSpan(), ASTPath(path), ::std::move(name)});
                    return;
                }
            } else {
                PUTBACK(tok, lex);
            }
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            break;
        case TOK_INTERPOLATED_TYPE: {
            if (!tok.fragType()->isPath()) {
                throw ParseErrorUnexpected(lex, tok);
            }
            auto& p = tok.fragType()->path();
            if (p.cls.is_UFCS()) {
                throw ParseErrorUnexpected(lex, tok);
            }
            path = std::move(tok.fragType()->path());
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
        } break;
        case TOK_INTERPOLATED_PATH:
            path = mv$(tok.fragPath());
            GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
            break;
        default:
            if (lex.editionAfter(ASTEdition::Rust2018)) {
                path = ASTPath::newRelative(/*hygine=*/{}, {});
            }
            PUTBACK(tok, lex);
            break;
    }

    ParseUseInner(lex, entries, path, explicitAbsolute);
}

ASTUseItem ParseUse(TokenStream& lex) {
    TRACE_FUNCTION;

    Token tok;
    ProtoSpan spanStart = lex.startSpan();

    ::std::vector<ASTUseItem::Ent> entries;

    if (lex.lookahead(0) == TOK_BRACE_OPEN) {
        GET_TOK(tok, lex);
        do {
            if (lex.lookahead(0) == TOK_BRACE_CLOSE) {
                GET_TOK(tok, lex);
                break;
            }
            ParseUseRoot(lex, entries);
        } while (GET_TOK(tok, lex) == TOK_COMMA);
        CHECK_TOK(tok, TOK_BRACE_CLOSE);
    } else {
        ParseUseRoot(lex, entries);
    }

    return ASTUseItem{lex.endSpan(spanStart), mv$(entries)};
}

ASTFunction ParseDelegationFunction(TokenStream& lex, RcString& itemName) {
    Token tok;
    auto ps = lex.startSpan();
    ::std::vector<ASTUseItem::Ent> entries;

    if (lex.lookahead(0) == TOK_LT) {
        GET_TOK(tok, lex);
        auto type = ParseType(lex, true);
        ::std::unique_ptr<ASTPath> trait;
        if (lex.getTokenIf(TOK_RWORD_AS)) {
            trait = ::std::make_unique<ASTPath>(ParsePath(lex, PATH_GENERIC_TYPE));
        }
        GET_CHECK_TOK(tok, lex, TOK_GT);
        GET_CHECK_TOK(tok, lex, TOK_DOUBLE_COLON);
        auto path = trait
            ? ASTPath::newUfcsTrait(mv$(type), mv$(*trait), {})
            : ASTPath::newUfcsTy(mv$(type), {});
        ParseUseInner(lex, entries, path);
    } else {
        Ident::Hygiene relativeHygiene;
        const bool relativeRoot = lex.lookahead(0) == TOK_IDENT;
        if (relativeRoot) {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            relativeHygiene = tok.ident().hygiene;
            PUTBACK(tok, lex);
        }
        ParseUseRoot(lex, entries);
        if (relativeRoot) {
            for (auto& entry : entries) {
                if (entry.path.cls.is_Absolute() && entry.path.cls.as_Absolute().crate == "") {
                    auto nodes = mv$(entry.path.cls.as_Absolute().nodes);
                    entry.path = ASTPath::newRelative(relativeHygiene, mv$(nodes));
                }
            }
        }
    }

    ASTExpr body;
    if (lex.lookahead(0) == TOK_BRACE_OPEN) {
        body = ParseExprBlock(lex);
        struct MarkDelegationSelf: ASTNodeVisitorDef {
            void visit(ASTExprNodeMacro&) override {
                // Macro output is not the delegation body's magic `self`.
            }
            void visit(ASTExprNodeNamedValue& node) override {
                if (node.mPath.cls.is_Local() && node.mPath.cls.as_Local().name == "self") {
                    node.mPath = ASTPath(RcString::newInterned("#delegation-self"));
                }
            }
        } visitor;
        body.node().visit(visitor);
    } else {
        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
    }

    ASSERT_BUG(lex.endSpan(ps), !entries.empty(), "Empty delegation");
    ASTFunction::Delegation delegation;
    for (auto& entry : entries) {
        delegation.targets.push_back({mv$(entry.path), entry.name});
    }
    delegation.body = mv$(body);
    itemName = delegation.targets.size() == 1 ? delegation.targets.front().name : RcString();

    auto span = lex.endSpan(ps);
    auto fcn = ASTFunction(span, ABI_RUST, ASTFunction::Flags(), {}, mkType(lex.typePool(), span), {}, false);
    fcn.setDelegation(mv$(delegation));
    return fcn;
}

::std::vector<::std::pair<RcString, ASTFunction>> SplitDelegationFunction(const ASTFunction& fcn) {
    ASSERT_BUG(fcn.sp(), fcn.delegation(), "Splitting a non-delegation function");
    ::std::vector<::std::pair<RcString, ASTFunction>> rv;
    for (size_t i = 0; i < fcn.delegation()->targets.size(); i++) {
        auto split = fcn.clone();
        auto delegation = split.takeDelegation();
        auto target = mv$(delegation->targets[i]);
        delegation->targets.clear();
        delegation->targets.push_back(mv$(target));
        auto name = delegation->targets.front().name;
        split.setDelegation(mv$(*delegation));
        rv.push_back(::std::make_pair(mv$(name), mv$(split)));
    }
    return rv;
}

ASTMacroInvocation ParseMacroInvocation(ProtoSpan spanStart, ASTPath name, TokenStream& lex) {
    Token tok;
    RcString ident;
    if (GET_TOK(tok, lex) == TOK_IDENT) {
        ident = tok.ident().name;
    } else {
        PUTBACK(tok, lex);
    }
    bool isMacro = (name.isTrivial() && name.asTrivial() == "macro_rules");

    if (isMacro) {
        lex.pushHygine();
    }
    TokenTree tt = ParseTT(lex, true);
    if (tt.isToken()) {
        throw ParseErrorUnexpected(lex, tt.tok());
    }
    if (isMacro) {
        lex.popHygine();
    }
    DEBUG("name=" << name << ", ident=" << ident << ", tt=" << tt);
    return ASTMacroInvocation(lex.endSpan(spanStart), mv$(name), mv$(ident), mv$(tt));
}

bool ParseMacroInvocationOpt(TokenStream& lex, ASTMacroInvocation& outInv) {
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

    auto ps = lex.startSpan();
    auto namePath = ParsePath(lex, PATH_GENERIC_NONE);
    GET_CHECK_TOK(tok, lex, TOK_EXCLAM);

    bool isBraced = (lex.lookahead(0) == TOK_BRACE_OPEN || (lex.lookahead(0) == TOK_IDENT && lex.lookahead(1) == TOK_BRACE_OPEN));

    outInv = ParseMacroInvocation(ps, namePath, lex);

    if (!isBraced) {
        GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
    }
    return true;
}

ASTNamed<ASTItem> ParseModItemS(TokenStream& lex, const ASTModule::FileInfo& modFileinfo, const ASTAbsolutePath& modPath, ASTAttributeList metaItems) {
    TRACE_FUNCTION_F("mod_path=" << modPath << ", meta_items=" << metaItems);
    Token tok;

    // NOTE: This assigns into a parameter, so can't use Parse_ItemAttrs
    while (LOOKAHEAD2(lex, TOK_HASH, TOK_SQUARE_OPEN)) {
        // Attributes!
        GET_CHECK_TOK(tok, lex, TOK_HASH);
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_OPEN);
        metaItems.push_back(ParseMetaItem(lex));
        GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
    }

    if (LOOK_AHEAD(lex) == TOK_INTERPOLATED_ITEM) {
        GET_TOK(tok, lex);
        auto rv = tok.takeFragItem();
        // Transfer new attributes onto the item
        for (auto& mi : metaItems.mItems) {
            rv.attrs.mItems.push_back(mv$(mi));
        }
        return rv;
    }

    auto ps = lex.startSpan();

    {
        ASTMacroInvocation inv;
        if (ParseMacroInvocationOpt(lex, inv)) {
            return ASTNamed<ASTItem>{lex.endSpan(ps), mv$(metaItems), ASTVisibility::makeGlobal(), "", ASTItem(mv$(inv))};
        }
    }

    RcString itemName;
    ASTItem itemData;

    auto vis = ParsePublicity(lex);

    switch (GET_TOK(tok, lex)) {
        case TOK_RWORD_USE:
            itemData = ParseUse(lex);
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
                            itemName = tok.ident().name;
                            itemData = ASTItem(ParseFunctionDefWithCode(lex, /*allow_self=*/false, abi, ASTFunction::Flags()));
                            break;
                        }
                        // `extern "ABI" {`
                        case TOK_BRACE_OPEN:
                            itemName = "";
                            itemData = ASTItem(ParseExternBlock(lex, mv$(abi), metaItems));
                            break;
                        default:
                            throw ParseErrorUnexpected(lex, tok, {TOK_RWORD_FN, TOK_BRACE_OPEN});
                    }
                    break;
                }
                // `extern fn ...`
                case TOK_RWORD_FN:
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    itemName = tok.ident().name;
                    itemData = ASTItem(ParseFunctionDefWithCode(lex, /*allow_self=*/false, "C", ASTFunction::Flags()));
                    break;

                // NOTE: `extern { ...` is handled in caller
                case TOK_BRACE_OPEN:
                    itemName = "";
                    itemData = ASTItem(ParseExternBlock(lex, "C", metaItems));
                    break;

                // `extern crate "crate-name" as crate_name;`
                // `extern crate crate_name;`
                // `extern crate crate_name as other_name;`
                case TOK_RWORD_CRATE:
                    switch (GET_TOK(tok, lex)) {
                        case TOK_RWORD_SELF:
                            itemData = ASTItem::make_Crate({RcString::newInterned("")});
                            if (lex.getTokenIf(TOK_RWORD_AS)) {
                                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                                itemName = tok.ident().name;
                            } else {
                                itemName = RcString::newInterned("self");
                            }
                            break;
                        // `extern crate "crate-name" as crate_name;`
                        // NOTE: rustc doesn't allow this, keep in mrustc for for reparse support
                        case TOK_STRING:
                            itemData = ASTItem::make_Crate({RcString::newInterned(tok.str())});
                            GET_CHECK_TOK(tok, lex, TOK_RWORD_AS);
                            GET_CHECK_TOK(tok, lex, TOK_IDENT);
                            itemName = tok.ident().name;
                            break;
                        // `extern crate crate_name;`
                        // `extern crate crate_name as other_name;`
                        case TOK_IDENT:
                            itemName = tok.ident().name;
                            if (GET_TOK(tok, lex) == TOK_RWORD_AS) {
                                itemData = ASTItem::make_Crate({mv$(itemName)});

                                itemName = getOptionalIdent(lex);
                            } else {
                                PUTBACK(tok, lex);
                                itemData = ASTItem::make_Crate({itemName});
                            }
                            break;
                        default:
                            throw ParseErrorUnexpected(lex, tok, {TOK_STRING, TOK_IDENT});
                    }
                    GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
                    break;
                default:
                    throw ParseErrorUnexpected(lex, tok, {TOK_STRING, TOK_RWORD_FN, TOK_BRACE_OPEN, TOK_RWORD_CRATE});
            }
            break;

        // `const NAME`
        // `const [unsafe] fn`
        case TOK_RWORD_CONST:
            switch (GET_TOK(tok, lex)) {
                case TOK_UNDERSCORE: // 1.39?
                case TOK_IDENT: {
                    PUTBACK(tok, lex);
                    itemName = getOptionalIdent(lex);

                    GET_CHECK_TOK(tok, lex, TOK_COLON);
                    ASTType* type = ParseType(lex);
                    GET_CHECK_TOK(tok, lex, TOK_EQUAL);
                    ASTExpr val = ParseExpr(lex);
                    GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
                    itemData = ASTItem(ASTStatic(ASTStatic::CONST, mv$(type), mv$(val)));
                    break;
                }
                case TOK_RWORD_UNSAFE: {
                    auto abi = ::std::string(ABI_RUST);
                    if (lex.getTokenIf(TOK_RWORD_EXTERN)) {
                        abi = lex.lookahead(0) == TOK_STRING ? lex.getToken().str() : "C";
                    }
                    GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    itemName = tok.ident().name;
                    itemData = ASTItem(ParseFunctionDefWithCode(lex, /*allow_self=*/false, abi, ASTFunction::Flags().setConst().setUnsafe()));
                    break;
                }
                case TOK_RWORD_ASYNC: {
                    auto flags = ASTFunction::Flags().setConst().setAsync();
                    if (lex.getTokenIf(TOK_RWORD_UNSAFE)) {
                        flags = flags.setUnsafe();
                    }
                    auto abi = ::std::string(ABI_RUST);
                    if (lex.getTokenIf(TOK_RWORD_EXTERN)) {
                        abi = lex.lookahead(0) == TOK_STRING ? lex.getToken().str() : "C";
                    }
                    GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    itemName = tok.ident().name;
                    itemData = ASTItem(ParseFunctionDefWithCode(lex, /*allow_self=*/false, abi, flags));
                    break;
                }
                case TOK_RWORD_EXTERN: {
                    auto abi = lex.lookahead(0) == TOK_STRING ? lex.getToken().str() : "C";
                    GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    itemName = tok.ident().name;
                    itemData = ASTItem(ParseFunctionDefWithCode(lex, /*allow_self=*/false, abi, ASTFunction::Flags().setConst()));
                    break;
                }
                case TOK_RWORD_FN:
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    itemName = tok.ident().name;
                    // - self not allowed, not prototype
                    itemData = ASTItem(ParseFunctionDefWithCode(lex, /*allow_self=*/false, ABI_RUST, ASTFunction::Flags().setConst()));
                    break;
                default:
                    throw ParseErrorUnexpected(lex, tok, {TOK_IDENT, TOK_UNDERSCORE, TOK_RWORD_UNSAFE, TOK_RWORD_FN});
            }
            break;
        // `static NAME`
        // `static mut NAME`
        case TOK_RWORD_STATIC: {
            bool isMut = false;
            if (GET_TOK(tok, lex) == TOK_RWORD_MUT) {
                isMut = true;
                GET_TOK(tok, lex);
            }
            PUTBACK(tok, lex);
            itemName = getOptionalIdent(lex);

            GET_CHECK_TOK(tok, lex, TOK_COLON);
            ASTType* type = ParseType(lex);

            GET_CHECK_TOK(tok, lex, TOK_EQUAL);

            ASTExpr val = ParseExpr(lex);

            GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);
            itemData = ASTItem(ASTStatic((isMut ? ASTStatic::MUT : ASTStatic::STATIC), mv$(type), mv$(val)));
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
                        itemName = "";
                        itemData = ASTItem(ParseExternBlock(lex, "C", metaItems));
                    } else {
                        GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
                        GET_CHECK_TOK(tok, lex, TOK_IDENT);
                        itemName = tok.ident().name;
                        itemData = ASTItem(ParseFunctionDefWithCode(lex, false, abi, ASTFunction::Flags().setUnsafe()));
                    }
                    break;
                }
                // `unsafe fn`
                case TOK_RWORD_FN:
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    itemName = tok.ident().name;
                    // - self not allowed, not prototype
                    itemData = ASTItem(ParseFunctionDefWithCode(lex, false, ABI_RUST, ASTFunction::Flags().setUnsafe()));
                    break;
                // `unsafe trait`
                case TOK_RWORD_TRAIT: {
                    GET_CHECK_TOK(tok, lex, TOK_IDENT);
                    itemName = tok.ident().name;
                    auto tr = ParseTraitDef(lex, metaItems, ParseGenericParamsOpt(lex));
                    tr.setIsUnsafe();
                    itemData = ASTItem(::std::move(tr));
                    break;
                }
                // `unsafe impl`
                case TOK_RWORD_IMPL: {
                    auto impl = ParseImpl(lex, metaItems, true);
                    if (impl.is_Impl()) {
                        impl.as_Impl().def().setIsUnsafe();
                    } else if (impl.is_NegImpl()) {
                        impl.as_NegImpl().setIsUnsafe();
                    } else {
                        BUG(lex.pointSpan(), "Parse_Impl returned a variant other than Impl or NegImpl");
                    }
                    return ASTNamed<ASTItem>{Span(), mv$(metaItems), ASTVisibility::makeGlobal(), "", mv$(impl)};
                }
                // `unsafe auto trait`
                case TOK_IDENT:
                    if (tok.ident().name == "auto") {
                        GET_CHECK_TOK(tok, lex, TOK_RWORD_TRAIT);
                        GET_CHECK_TOK(tok, lex, TOK_IDENT);
                        itemName = tok.ident().name;
                        auto tr = ParseTraitDef(lex, metaItems, ParseGenericParamsOpt(lex));
                        tr.setIsUnsafe();
                        tr.setIsMarker();
                        itemData = ASTItem(::std::move(tr));
                        break;
                    }
                    //goto default;
                default:
                    throw ParseErrorUnexpected(lex, tok, {TOK_RWORD_FN, TOK_RWORD_TRAIT, TOK_RWORD_IMPL});
            }
            break;
        case TOK_RWORD_ASYNC: {
            ASTFunction::Flags flags;
            flags.isAsync = true;
            ;
            if (lex.getTokenIf(TOK_RWORD_UNSAFE)) {
                flags.isUnsafe = true;
            }
            GET_CHECK_TOK(tok, lex, TOK_RWORD_FN);
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            itemName = tok.ident().name;
            // - self not allowed, not prototype
            itemData = ASTItem(ParseFunctionDefWithCode(lex, false, ABI_RUST, flags));
            break;
        }
        // `fn`
        case TOK_RWORD_FN:
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            itemName = tok.ident().name;
            // - self not allowed, not prototype
            itemData = ASTItem(ParseFunctionDefWithCode(lex, false, ABI_RUST, ASTFunction::Flags()));
            break;
        // `type`
        case TOK_RWORD_TYPE:
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            itemName = tok.ident().name;
            itemData = ASTItem(ParseTypeAlias(lex));
            break;
        // `struct`
        case TOK_RWORD_STRUCT:
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            itemName = tok.ident().name;
            itemData = ASTItem(ParseStruct(lex, metaItems));
            break;
        // `enum`
        case TOK_RWORD_ENUM:
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            itemName = tok.ident().name;
            itemData = ASTItem(ParseEnumDef(lex, metaItems));
            break;

        // Contextual keywords
        case TOK_IDENT:
            if (tok.ident().name == "union") {
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                itemName = tok.ident().name;
                itemData = ASTItem(ParseUnion(lex, metaItems));
            }
            // `auto trait`
            else if (tok.ident().name == "auto") {
                GET_CHECK_TOK(tok, lex, TOK_RWORD_TRAIT);
                GET_CHECK_TOK(tok, lex, TOK_IDENT);
                itemName = tok.ident().name;
                auto tr = ParseTraitDef(lex, metaItems, ParseGenericParamsOpt(lex));
                tr.setIsMarker();
                itemData = ASTItem(::std::move(tr));
            } else if (tok.ident().name == "reuse") {
                itemData = ASTItem(ParseDelegationFunction(lex, itemName));
            } else {
                throw ParseErrorUnexpected(lex, tok);
            }
            break;

        // `impl`
        case TOK_RWORD_IMPL: {
            auto impl = ParseImpl(lex, metaItems);
            return ASTNamed<ASTItem>{Span(), std::move(metaItems), ASTVisibility::makeGlobal(), "", std::move(impl)};
        }
        // `trait`
        case TOK_RWORD_TRAIT: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            itemName = tok.ident().name;
            ASTGenericParams params = ParseGenericParamsOpt(lex);
            if (lex.lookahead(0) == TOK_EQUAL) {
                // Trait alias (can't be auto or unsafe?)

                ASTTraitAlias rv;
                rv.params = std::move(params);
                lex.getToken();
                if (lex.lookahead(0) != TOK_RWORD_WHERE) {
                    for (;;) {
                        if (lex.lookahead(0) == TOK_LIFETIME) {
                            auto ps = lex.startSpan();
                            GET_CHECK_TOK(tok, lex, TOK_LIFETIME);
                            rv.lifetimes.push_back(Spanned<ASTLifetimeRef>{lex.endSpan(ps), ASTLifetimeRef(tok.ident())});
                        } else {
                            auto ps = lex.startSpan();
                            auto hrbs = ParseHRBOpt(lex);
                            rv.traits.push_back(GET_SPANNED(TypeTraitPath, lex, (TypeTraitPath(mv$(hrbs), ParsePath(lex, PATH_GENERIC_TYPE)))));
                        }
                        if (!lex.getTokenIf(TOK_PLUS)) {
                            break;
                        }
                    }
                }
                if (lex.getTokenIf(TOK_RWORD_WHERE)) {
                    ParseWhereClause(lex, rv.params);
                }

                GET_CHECK_TOK(tok, lex, TOK_SEMICOLON);

                itemData = ASTItem(std::move(rv));
            } else {
                itemData = ASTItem(ParseTraitDef(lex, metaItems, std::move(params)));
            }
            break;
        }

        case TOK_RWORD_MACRO: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto name = tok.ident().name;
            DEBUG("name = " << name);
            MacroRulesPtr mrp;
            if (lex.lookahead(0) == TOK_BRACE_OPEN) {
                GET_TOK(tok, lex);
                mrp = ParseMacroRules(lex);
            } else if (lex.lookahead(0) == TOK_PAREN_OPEN) {
                mrp = ParseMacroRulesSingleArm(lex);
            } else {
                GET_TOK(tok, lex);
                throw ParseErrorUnexpected(lex, tok);
            }

            {
                Ident::ModPath mp;
                mp.crate = "";
                mp.ents = modPath.nodes;
                mrp->mHygiene.setModPath(lex.typePool(), ::std::move(mp));
                mrp->isMacroItem = true;
            }

            itemName = name;
            itemData = ASTItem(mv$(mrp));
        } break;

        case TOK_RWORD_MOD: {
            GET_CHECK_TOK(tok, lex, TOK_IDENT);
            auto name = tok.ident().name;
            DEBUG("Sub module '" << name << "'");
            ASTModule submod(modPath + name);

            // Check #[cfg] and don't load if it fails
            struct H {
                static bool checkItemCfg(const Settings& settings, const ASTAttributeList& attrs) {
                    for (const auto& at : attrs.mItems) {
                        if (at.name() == "cfg" && !checkCfg(settings, at.span(), at)) {
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
            ::std::string pathAttr;
            for (const auto& a : metaItems.mItems) {
                DEBUG("[mod path_attr] " << a);
                if (a.name() == "path") {
                    pathAttr = a.parseEqualsString(*lex.parseState().wb, *lex.parseState().crate, *lex.parseState().module);
                } else if (a.name() == "cfg_attr") {
                    for (const auto& a2 : checkCfgAttr(*lex.parseState().wb->settings, a)) {
                        DEBUG("[mod path_attr cfg_attr] " << a2);
                        if (a2.name() == "path") {
                            pathAttr = a2.parseEqualsString(*lex.parseState().wb, *lex.parseState().crate, *lex.parseState().module);
                        }
                    }
                } else {
                }
            }
            DEBUG("path_attr = \"" << pathAttr << "\"");

            FsPath subPath;
            bool subFileControlsDir = true;
            if (modFileinfo.path == "-") {
                if (pathAttr.size()) {
                    ERROR(lex.pointSpan(), E0000, "Cannot load module from file when reading stdin");
                }
                subPath = "-";
            } else if (pathAttr.size() > 0) {
                // If in a local mod, then use this arm
                bool inSubmod = modFileinfo.path[modFileinfo.path.size() - 1] == '/';
                if (modFileinfo.inModBlock) {
                    // REF: `rustc-1.90.0-src/vendor/hashbrown-0.14.5/src/lib.rs:63`
                    subPath = dirname(modFileinfo.path) / pathAttr.c_str();
                } else {
                    // Otherwise use this:
                    // REF: `rustc-1.90.0-src/vendor/icu_list_data-1.5.1/data/macros.rs:30`
                    subPath = dirname(lex.pointSpan().getTopFileSpan().filename.c_str()) / pathAttr.c_str();
                }
            } else if (modFileinfo.controlsDir) {
                subPath = dirname(modFileinfo.path) / name.c_str();
            } else {
                subPath = dirname(modFileinfo.path) / modPath.nodes.back().c_str() / name.c_str();
                subFileControlsDir = false;
            }
            DEBUG("Mod '" << name << "', sub_path = " << subPath);

            submod.fileInfo.path = subPath;
            submod.fileInfo.controlsDir = subFileControlsDir;

            switch (GET_TOK(tok, lex)) {
                case TOK_BRACE_OPEN:
                    submod.fileInfo.path = subPath.str() + "/";
                    submod.fileInfo.inModBlock = true;
                    submod.fileInfo.isDisabled = !H::checkItemCfg(*lex.parseState().wb->settings, metaItems);
                    // TODO: If cfg fails, just eat the TT until a matching #[cfg]?
                    // - Or, mark the file infor as not being valid (so child modules don't try to load)
                    ParseModRoot(lex, submod, metaItems);
                    GET_CHECK_TOK(tok, lex, TOK_BRACE_CLOSE);
                    break;
                case TOK_SEMICOLON:
                    if (modFileinfo.isDisabled) {
                    } else if (subPath.str() == "-") {
                        ERROR(lex.pointSpan(), E0000, "Cannot load module from file when reading stdin");
                    } else if (!H::checkItemCfg(*lex.parseState().wb->settings, metaItems)) {
                        // Ignore - emit Item::None
                        itemName = mv$(name);
                        itemData = ASTItem();
                        break;
                    } else if (pathAttr.size() == 0 && !modFileinfo.controlsDir) {
                        ASSERT_BUG(lex.pointSpan(), modPath.nodes.size() >= 1, "Crate root should control its directory?");
                        // Look for `curdir/curmod/submod.rs` or `curdir/curmod/submod/mod.rs`
                        ::std::string newpathFileDirect = dirname(modFileinfo.path) / modPath.nodes.back().c_str() / name.c_str() + ".rs";
                        ::std::string newpathFileMod = dirname(modFileinfo.path) / modPath.nodes.back().c_str() / name.c_str() / "mod.rs";
                        DEBUG(modFileinfo.path << " " << modPath);
                        DEBUG("newpath_file_direct = '" << newpathFileDirect << "'");
                        DEBUG("newpath_file_mod = '" << newpathFileMod << "'");

                        ::std::ifstream ifsFile(newpathFileDirect);
                        ::std::ifstream ifsDir(newpathFileMod);

                        if (ifsDir.is_open() && ifsFile.is_open()) {
                            // Collision
                            ERROR(lex.pointSpan(), E0000, "Both modname.rs and modname/mod.rs exist");
                        } else if (ifsDir.is_open()) {
                            // Load from dir
                            submod.fileInfo.path = newpathFileMod;
                            submod.fileInfo.controlsDir = true;
                        } else if (ifsFile.is_open()) {
                            submod.fileInfo.path = newpathFileDirect;
                            submod.fileInfo.controlsDir = false;
                        } else {
                            // Can't find file
                            ERROR(lex.pointSpan(), E0000, "Can't find file for '" << name << "' in '" << modFileinfo.path << "'");
                        }
                        DEBUG("- path = " << submod.fileInfo.path);
                        Lexer subLex(lex.typePool(), submod.fileInfo.path, lex.getEdition(), lex.parseState());
                        ParseModRoot(subLex, submod, metaItems);
                        GET_CHECK_TOK(tok, subLex, TOK_EOF);
                    } else {
                        ::std::string newpathDir = subPath.str() + "/";
                        ::std::string newpathFile = pathAttr.size() > 0 ? subPath : subPath + ".rs";
                        DEBUG("newpath_dir = '" << newpathDir << "', newpath_file = '" << newpathFile << "'");
                        ::std::ifstream ifsDir(newpathDir + "mod.rs");
                        ::std::ifstream ifsFile(newpathFile);
                        if (ifsDir.is_open() && ifsFile.is_open()) {
                            // Collision
                            ERROR(lex.pointSpan(), E0000, "Both modname.rs and modname/mod.rs exist");
                        } else if (ifsDir.is_open()) {
                            // Load from dir
                            submod.fileInfo.path = newpathDir + "mod.rs";
                        } else if (ifsFile.is_open()) {
                            submod.fileInfo.path = newpathFile;
                            if (pathAttr == "") {
                                submod.fileInfo.controlsDir = false;
                            }
                        }
                        // TODO: If this is not a controlling file, look in `modname/` for the new module
                        else {
                            // Can't find file
                            ERROR(lex.pointSpan(), E0000, "Can't find file for '" << name << "' in '" << modFileinfo.path << "'");
                        }
                        DEBUG("- path = " << submod.fileInfo.path);
                        Lexer subLex(lex.typePool(), submod.fileInfo.path, lex.getEdition(), lex.parseState());
                        ParseModRoot(subLex, submod, metaItems);
                        GET_CHECK_TOK(tok, subLex, TOK_EOF);
                    }
                    break;
                default:
                    throw CompileErrorGeneric("Expected { or ; after module name");
            }
            itemName = mv$(name);
            itemData = ASTItem(mv$(submod));
            break;
        }

        default:
            throw ParseErrorUnexpected(lex, tok);
    }

    return ASTNamed<ASTItem>{lex.endSpan(ps), mv$(metaItems), vis, mv$(itemName), mv$(itemData)};
}

void ParseModItem(TokenStream& lex, ASTModule& mod, ASTAttributeList metaItems) {
    SET_MODULE(lex, mod);
    lex.parseState().module = &mod;
    lex.parseState().parentAttrs = &metaItems;

    auto item = ParseModItemS(lex, mod.fileInfo, mod.path(), mv$(metaItems));
    if (item.data.is_Function() && item.data.as_Function().delegation()
        && item.data.as_Function().delegation()->targets.size() > 1) {
        for (auto& split : SplitDelegationFunction(item.data.as_Function())) {
            mod.addItem(item.span, item.vis, mv$(split.first), ASTItem(mv$(split.second)), item.attrs.clone());
        }
    } else {
        mod.addItem(mv$(item));
    }
}

void ParseModRootItems(TokenStream& lex, ASTModule& mod) {
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
        auto metaItems = ParseItemAttrs(lex);
        DEBUG("meta_items = " << metaItems);

        ParseModItem(lex, mod, mv$(metaItems));
    }
}

void ParseModRoot(TokenStream& lex, ASTModule& mod, ASTAttributeList& modAttrs) {
    TRACE_FUNCTION;

    auto prevMod = lex.parseState().module;
    lex.parseState().module = &mod;
    // Attributes on module/crate (will continue loop)
    ParseParentAttrs(lex, modAttrs);

    ParseModRootItems(lex, mod);
    lex.parseState().module = prevMod;
}

ASTCrate* ParseCrate(const WireBoard& wb, stl::ObjPool* pool, ::std::string mainfile, ASTEdition edition) {
    Token tok;

    Lexer lex(*wb.pool, mainfile, edition, ParseState());

    size_t p = mainfile.find_last_of('/');
    p = (p == ::std::string::npos ? mainfile.find_last_of('\\') : p);
    ::std::string mainpath = mainfile == "-" ? "-" : (p != ::std::string::npos ? ::std::string(mainfile.begin(), mainfile.begin() + p + 1) : "./");

    auto* crate = pool->make<ASTCrate>(pool, *wb.types);
    crate->edition = edition;

    crate->rootModule().fileInfo.path = mainpath;
    crate->rootModule().fileInfo.controlsDir = true;

    lex.parseState().crate = crate;
    lex.parseState().wb = &wb;
    ParseModRoot(lex, crate->rootModule(), crate->mAttrs);

    return crate;
}

#undef GET_SPANNED
#undef LOOKAHEAD2

// === PROTOTYPES ===
ASTType* ParseTypeInt(TokenStream& lex, bool allowTraitList);
ASTType* ParseTypeFn(TokenStream& lex, ASTHigherRankedBounds hrbs = {});
ASTType* ParseTypePath(TokenStream& lex, ASTHigherRankedBounds hrbs, bool allowTraitList);
ASTType* ParseTypeTraitObject(TokenStream& lex, ASTHigherRankedBounds hrbs = {});
ASTType* ParseTypeErasedType(TokenStream& lex, bool allowTraitList);

// === CODE ===
ASTType* ParseType(TokenStream& lex, bool allowTraitList) {
    ASTType* rv = ParseTypeInt(lex, allowTraitList);
    if (lex.lookahead(0) == TOK_IDENT) {
        auto tok = lex.getToken();
        if (tok.ident().name == "is") {
            auto pat = ParsePattern(lex, AllowOrPattern::Yes);
            return mkType(lex.typePool(), rv->span(), TypeData::make_Pattern({rv, lex.typePool().make<ASTPattern>(mv$(pat))}));
        }
        lex.putback(mv$(tok));
    }
    return rv;
}

ASTType* ParseTypeInt(TokenStream& lex, bool allowTraitList) {
    //TRACE_FUNCTION;
    auto ps = lex.startSpan();

    Token tok;

    switch (GET_TOK(tok, lex)) {
        case TOK_INTERPOLATED_TYPE:
            return mv$(tok.fragType());
        // '!' - Only ever used as part of function prototypes, but is kinda a type... not allowed here though
        case TOK_EXCLAM:
            return mkType(lex.typePool(), lex.subSpan(tok.getPos()), TypeData::make_Bang({}));
        // '_' = Wildcard (type inferrence variable)
        case TOK_UNDERSCORE:
            return mkType(lex.typePool(), lex.subSpan(tok.getPos()));

        // 'unsafe' - An unsafe function type
        case TOK_RWORD_UNSAFE:
        // 'extern' - A function type with an ABI
        case TOK_RWORD_EXTERN:
        // 'fn' - Rust function
        case TOK_RWORD_FN:
            PUTBACK(tok, lex);
            return ParseTypeFn(lex);

        case TOK_RWORD_IMPL:
            return ParseTypeErasedType(lex, allowTraitList);

        // '<' - An associated type cast
        case TOK_LT:
        case TOK_THINARROW_LEFT:
        case TOK_DOUBLE_LT: {
            PUTBACK(tok, lex);
            auto path = ParsePath(lex, PATH_GENERIC_TYPE);
            return mkType(lex.typePool(), ASTTypeTags::Path(), lex.endSpan(ps), mv$(path));
        }
        case TOK_RWORD_FOR: {
            auto hrls = ParseHRB(lex);
            switch (LOOK_AHEAD(lex)) {
                case TOK_RWORD_UNSAFE:
                case TOK_RWORD_EXTERN:
                case TOK_RWORD_FN:
                    return ParseTypeFn(lex, hrls);
                default:
                    return ParseTypePath(lex, hrls, true);
            }
        }
        case TOK_RWORD_DYN: {
            ASTHigherRankedBounds hrbs = ParseHRBOpt(lex);
            return ParseTypeTraitObject(lex, mv$(hrbs));
        }
        // <ident> - Either a primitive, or a path
        case TOK_IDENT:
            // TODO: Only allow if the next token isn't `::` or `!`
            if (tok.ident().name == "dyn") {
                ASTHigherRankedBounds hrbs = ParseHRBOpt(lex);
                return ParseTypeTraitObject(lex, mv$(hrbs));
            }
            // or a primitive
            //if( auto ct = coretype_fromstring(tok.str()) )
            //{
            //}
            PUTBACK(tok, lex);
            return ParseTypePath(lex, {}, allowTraitList);
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
            return ParseTypePath(lex, {}, allowTraitList);

        // The lexer maximally tokenizes `&&`; reference-type parsing consumes it as two `&` tokens.
        case TOK_DOUBLE_AMP:
            lex.putback(Token(TOK_AMP));
        // '&' - Reference type
        case TOK_AMP: {
            ASTLifetimeRef lifetime;
            // Reference
            tok = lex.getToken();
            if (tok.type() == TOK_LIFETIME) {
                lifetime = ASTLifetimeRef(/*lex.point_span(), */ tok.ident());
                tok = lex.getToken();
            }
            bool isMut = false;
            if (tok.type() == TOK_RWORD_MUT) {
                isMut = true;
            } else {
                PUTBACK(tok, lex);
            }
            return mkType(lex.typePool(), ASTTypeTags::Reference(), lex.endSpan(ps), ::std::move(lifetime), isMut, ParseType(lex, false));
        }
        // '*' - Raw pointer
        case TOK_STAR:
            // Pointer
            switch (GET_TOK(tok, lex)) {
                case TOK_RWORD_MUT:
                    // Mutable pointer
                    return mkType(lex.typePool(), ASTTypeTags::Pointer(), lex.endSpan(ps), true, ParseType(lex, false));
                case TOK_RWORD_CONST:
                    // Immutable pointer
                    return mkType(lex.typePool(), ASTTypeTags::Pointer(), lex.endSpan(ps), false, ParseType(lex, false));
                default:
                    throw ParseErrorUnexpected(lex, tok, {TOK_RWORD_CONST, TOK_RWORD_MUT});
            }
            throw CompileErrorBugCheck("Reached end of Parse_Type:STAR");
        // '[' - Array type
        case TOK_SQUARE_OPEN: {
            // Array
            ASTType* inner = ParseType(lex);
            if (GET_TOK(tok, lex) == TOK_SEMICOLON) {
                // Inferred size - unspecified
                if (lex.getTokenIf(TOK_UNDERSCORE)) {
                    GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                    return mkType(lex.typePool(), ASTTypeTags::SizedArray(), lex.endSpan(ps), mv$(inner), nullptr);
                } else {
                    // Sized array
                    ASTExpr arraySize = ParseExpr(lex);
                    GET_CHECK_TOK(tok, lex, TOK_SQUARE_CLOSE);
                    return mkType(lex.typePool(), ASTTypeTags::SizedArray(), lex.endSpan(ps), mv$(inner), arraySize.takeNode());
                }
            } else if (tok.type() == TOK_SQUARE_CLOSE) {
                return mkType(lex.typePool(), ASTTypeTags::UnsizedArray(), lex.endSpan(ps), mv$(inner));
            } else {
                throw ParseErrorUnexpected(lex, tok /*, "; or ]"*/);
            }
        }

        // '(' - Tuple (or lifetime bounded trait)
        case TOK_PAREN_OPEN: {
            DEBUG("Tuple");
            if (GET_TOK(tok, lex) == TOK_PAREN_CLOSE) {
                return mkType(lex.typePool(), ASTTypeTags::Tuple(), lex.endSpan(ps), {});
            }
            PUTBACK(tok, lex);

            ASTType* inner = ParseType(lex, true);
            if (LOOK_AHEAD(lex) == TOK_PAREN_CLOSE) {
                // Type in parens, NOT a tuple
                GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
                return inner;
            } else {
                ::std::vector<ASTType*> types;
                types.push_back(mv$(inner));
                while (GET_TOK(tok, lex) == TOK_COMMA) {
                    if (GET_TOK(tok, lex) == TOK_PAREN_CLOSE) {
                        break;
                    } else {
                        PUTBACK(tok, lex);
                    }
                    types.push_back(ParseType(lex));
                }
                CHECK_TOK(tok, TOK_PAREN_CLOSE);
                return mkType(lex.typePool(), ASTTypeTags::Tuple(), lex.endSpan(ps), mv$(types));
            }
        }
        default:
            throw ParseErrorUnexpected(lex, tok);
    }
    throw CompileErrorBugCheck("Reached end of Parse_Type");
}

ASTType* ParseTypeFn(TokenStream& lex, ASTHigherRankedBounds hrbs) {
    auto ps = lex.startSpan();
    TRACE_FUNCTION;
    Token tok;

    ::std::string abi = "";
    bool isUnsafe = false;

    GET_TOK(tok, lex);

    // `unsafe`
    if (tok.type() == TOK_RWORD_UNSAFE) {
        isUnsafe = true;
        GET_TOK(tok, lex);
    }
    // `exern`
    if (tok.type() == TOK_RWORD_EXTERN) {
        if (GET_TOK(tok, lex) == TOK_STRING) {
            abi = tok.str();
            if (abi == "") {
                ERROR(lex.pointSpan(), E0000, "Empty ABI");
            }
            GET_TOK(tok, lex);
        } else {
            abi = "C";
        }
    }
    // `fn`
    CHECK_TOK(tok, TOK_RWORD_FN);

    ::std::vector<ASTType*> args;
    bool isVariadic = false;
    GET_CHECK_TOK(tok, lex, TOK_PAREN_OPEN);
    while (LOOK_AHEAD(lex) != TOK_PAREN_CLOSE) {
        if (LOOK_AHEAD(lex) == TOK_TRIPLE_DOT) {
            GET_TOK(tok, lex);
            isVariadic = true;
            break;
        }
        // Handle `ident: `
        if ((lex.lookahead(0) == TOK_IDENT || lex.lookahead(0) == TOK_UNDERSCORE) && lex.lookahead(1) == TOK_COLON) {
            GET_TOK(tok, lex);
            GET_TOK(tok, lex);
        }
        args.push_back(ParseType(lex));
        if (GET_TOK(tok, lex) != TOK_COMMA) {
            PUTBACK(tok, lex);
            break;
        }
    }
    GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);

    // `-> RetType`
    ASTType* retType = mkType(lex.typePool(), ASTTypeTags::Unit(), lex.pointSpan());
    if (GET_TOK(tok, lex) == TOK_THINARROW) {
        retType = ParseType(lex, false);
    } else {
        PUTBACK(tok, lex);
    }

    return mkType(lex.typePool(), ASTTypeTags::Function(), lex.endSpan(ps), mv$(hrbs), isUnsafe, mv$(abi), mv$(args), isVariadic, mv$(retType));
}

ASTType* ParseTypePath(TokenStream& lex, ASTHigherRankedBounds hrbs, bool allowTraitList) {
    Token tok;

    auto ps = lex.startSpan();

    auto path = ParsePath(lex, PATH_GENERIC_TYPE);
    if (lex.lookahead(0) == TOK_EXCLAM) {
        GET_CHECK_TOK(tok, lex, TOK_EXCLAM);
        return mkType(lex.typePool(), ASTTypeTags::Macro(), ParseMacroInvocation(ps, path, lex));
    } else if (hrbs.empty() && !allowTraitList) {
        return mkType(lex.typePool(), ASTTypeTags::Path(), lex.endSpan(ps), mv$(path));
    } else {
        ::std::vector<TypeTraitPath> traits;
        ::std::vector<ASTLifetimeRef> lifetimes;

        traits.push_back(TypeTraitPath{mv$(hrbs), mv$(path)});

        if (allowTraitList) {
            while (lex.getTokenIf(TOK_PLUS)) {
                if (lex.getTokenIf(TOK_LIFETIME, tok)) {
                    lifetimes.push_back(ASTLifetimeRef(/*lex.point_span(),*/ tok.ident()));
                } else {
                    if (lex.lookahead(0) == TOK_RWORD_FOR) {
                        hrbs = ParseHRB(lex);
                    }
                    traits.push_back({mv$(hrbs), ParsePath(lex, PATH_GENERIC_TYPE)});
                }
            }
        }

        if (!traits[0].hrbs.empty() || traits.size() > 1 || lifetimes.size() > 0) {
            if (lifetimes.empty()) {
                lifetimes.push_back(ASTLifetimeRef());
            }
            return mkType(lex.typePool(), lex.endSpan(ps), mv$(traits), mv$(lifetimes));
        } else {
            return mkType(lex.typePool(), ASTTypeTags::Path(), lex.endSpan(ps), mv$(*traits.at(0).path));
        }
    }
}

ASTType* ParseTypeTraitObject(TokenStream& lex, ASTHigherRankedBounds hrbs) {
    Token tok;
    auto ps = lex.startSpan();

    ::std::vector<TypeTraitPath> traits;
    ::std::vector<ASTLifetimeRef> lifetimes;

    for (;;) {
        bool isFirst = traits.empty() && lifetimes.empty();
        if (LOOK_AHEAD(lex) == TOK_LIFETIME) {
            GET_TOK(tok, lex);

            if (isFirst && !hrbs.empty()) {
                // TODO: Error
            }

            lifetimes.push_back(ASTLifetimeRef(/*lex.point_span(),*/ tok.ident()));
        } else {
            auto constness = ParseBoundConstness(lex);
            if (lex.getTokenIf(TOK_RWORD_FOR)) {
                hrbs = ParseHRB(lex);
            } else {
            }
            auto postHrbConstness = ParseBoundConstness(lex);
            if (postHrbConstness != ASTBoundConstness::Never) {
                constness = postHrbConstness;
            }

            bool isParen = lex.getTokenIf(TOK_PAREN_OPEN);

            traits.push_back({mv$(hrbs), ParsePath(lex, PATH_GENERIC_TYPE), constness});

            if (isParen) {
                GET_CHECK_TOK(tok, lex, TOK_PAREN_CLOSE);
            }
        }

        if (!lex.getTokenIf(TOK_PLUS)) {
            break;
        }
    }

    if (lifetimes.empty()) {
        lifetimes.push_back(ASTLifetimeRef());
    }
    return mkType(lex.typePool(), lex.endSpan(ps), mv$(traits), mv$(lifetimes));
}

ASTType* ParseTypeErasedType(TokenStream& lex, bool allowTraitList) {
    Token tok;

    auto ps = lex.startSpan();
    TypeErasedType rvData;
    rvData.isEdition2024OrLater = lex.editionAfter(ASTEdition::Rust2024);
    do {
        if (lex.getTokenIf(TOK_LIFETIME, tok)) {
            rvData.lifetimes.push_back(ASTLifetimeRef(/*lex.point_span(),*/ tok.ident()));
        } else if (lex.getTokenIf(TOK_QMARK)) {
            ASTHigherRankedBounds hrbs = ParseHRBOpt(lex);
            rvData.maybeTraits.push_back({mv$(hrbs), ParsePath(lex, PATH_GENERIC_TYPE)});
        } else if (lex.getTokenIf(TOK_PAREN_OPEN)) {
            ASTHigherRankedBounds hrbs = ParseHRBOpt(lex);
            rvData.traits.push_back({mv$(hrbs), ParsePath(lex, PATH_GENERIC_TYPE)});
            lex.getTokenCheck(TOK_PAREN_CLOSE);
        } else if (lex.getTokenIf(TOK_RWORD_USE)) {
            lex.getTokenCheck(TOK_LT);
            if (rvData.use) {
                ERROR(lex.pointSpan(), E0000, "Multiple `use` seen in erased type");
            }
            rvData.use.reset(new ASTPathParams(ParsePathGenericList(lex)));
        } else {
            auto constness = ParseBoundConstness(lex);
            ASTHigherRankedBounds hrbs = ParseHRBOpt(lex);
            auto postHrbConstness = ParseBoundConstness(lex);
            if (postHrbConstness != ASTBoundConstness::Never) {
                constness = postHrbConstness;
            }
            rvData.traits.push_back({mv$(hrbs), ParsePath(lex, PATH_GENERIC_TYPE), constness});
        }
    } while (lex.getTokenIf(TOK_PLUS));

    return mkType(lex.typePool(), lex.endSpan(ps), TypeData::make_ErasedType(lex.typePool().make<TypeErasedType>(mv$(rvData))));
}
