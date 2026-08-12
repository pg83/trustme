#pragma once

#include <iostream>
#include "parse_tokenstream.h"
#include "ast_ast.h"

#define GET_TOK(tok, lex) ((tok = lex.getToken()).type())
#define PUTBACK(tok, lex) lex.putback(::std::move(tok))
#define LOOK_AHEAD(lex) (lex.lookahead(0))
#define GET_CHECK_TOK(tok, lex, exp)                                \
    do {                                                            \
        if ((tok = lex.getToken()).type() != exp) {                 \
            DEBUG("GET_CHECK_TOK " << __FILE__ << ":" << __LINE__); \
            throw ParseError::Unexpected(lex, tok, Token(exp));     \
        }                                                           \
    } while (0)
#define CHECK_TOK(tok, exp)                                     \
    do {                                                        \
        if (tok.type() != exp) {                                \
            DEBUG("CHECK_TOK " << __FILE__ << ":" << __LINE__); \
            throw ParseError::Unexpected(lex, tok, Token(exp)); \
        }                                                       \
    } while (0)

// --- path.cpp
enum eParsePathGenericMode {
    PATH_GENERIC_NONE,
    PATH_GENERIC_EXPR,
    PATH_GENERIC_TYPE
};

extern AST::Path ParsePath(TokenStream& lex, eParsePathGenericMode genericMode); // Auto-determines
extern AST::Path ParsePath(TokenStream& lex, bool isAbs, eParsePathGenericMode genericMode);
extern ::std::vector<AST::PathNode> ParsePathNodes(TokenStream& lex, eParsePathGenericMode genericMode);
extern AST::PathParams ParsePathGenericList(TokenStream& lex);

extern AST::Visibility ParsePublicity(TokenStream& lex, bool allowRestricted = true);
extern AST::HigherRankedBounds ParseHRB(TokenStream& lex);
extern ::AST::HigherRankedBounds ParseHRBOpt(TokenStream& lex);
extern AST::AttributeList ParseItemAttrs(TokenStream& lex);
extern void ParseParentAttrs(TokenStream& lex, AST::AttributeList& out);
extern AST::Attribute ParseMetaItem(TokenStream& lex);
extern ::AST::MacroInvocation ParseMacroInvocation(ProtoSpan ps, AST::Path path, TokenStream& lex);
extern TypeRef ParseType(TokenStream& lex, bool allowTraitList = true);
enum class AllowOrPattern {
    No,
    Yes,
};
extern AST::Pattern ParsePattern(TokenStream& lex, AllowOrPattern allowOr = AllowOrPattern::Yes);

extern void ParseImplItem(TokenStream& lex, AST::Impl& impl);
extern AST::Named<AST::Item> ParseTraitItem(TokenStream& lex);
extern void ParseModItem(TokenStream& lex, AST::Module& mod, AST::AttributeList meta_items);
extern ::AST::Named<::AST::Item> ParseModItemS(TokenStream& lex, const AST::Module::FileInfo& mod_fileinfo, const ::AST::AbsolutePath& mod_path, AST::AttributeList meta_items);
extern void ParseModRootItems(TokenStream& lex, AST::Module& mod);
extern ::AST::Named<::AST::Item> ParseExternBlockItem(TokenStream& lex, const std::string& abi);

extern AST::Expr ParseExpr(TokenStream& lex);
extern AST::Expr ParseExprBlock(TokenStream& lex);
extern AST::ExprNodeP ParseExpr0(TokenStream& lex);
extern AST::ExprNodeP ParseExpr13(TokenStream& lex); // Unaries
extern AST::ExprNodeP ParseExprVal(TokenStream& lex);
//extern AST::ExprNodeP Parse_ExprBlockNode(TokenStream& lex, AST::ExprNodeBlock::Type ty=AST::ExprNodeBlock::Type::Bare, Ident label=Ident(""));
extern AST::ExprNodeP ParseExprBlockNode(TokenStream& lex);
extern AST::ExprNodeP ParseExprBlockLine(TokenStream& lex, bool* addSilence);
extern AST::ExprNodeP ParseExprBlockLineWithItems(TokenStream& lex, ::std::shared_ptr<AST::Module>& local_mod, bool& addSilenceIfEnd);
extern AST::ExprNodeP ParseStmt(TokenStream& lex);

// unwrapped = Exclude the enclosing brackets (used by macro parse code)
extern TokenTree ParseTT(TokenStream& lex, bool unwrapped);

extern bool ParseIsTokValue(eTokenType tok_type);
