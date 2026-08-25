#pragma once

#include "ast_ast.h"
#include "parse_tokenstream.h"

#include <iostream>

#define GET_TOK(tok, lex) ((tok = lex.getToken()).type())
#define PUTBACK(tok, lex) lex.putback(::std::move(tok))
#define LOOK_AHEAD(lex) (lex.lookahead(0))
#define GET_CHECK_TOK(tok, lex, exp)                                \
    do {                                                            \
        if ((tok = lex.getToken()).type() != exp) {                 \
            DEBUG("GET_CHECK_TOK " << __FILE__ << ":" << __LINE__); \
            parseErrorUnexpected(lex, tok, Token(exp));       \
        }                                                           \
    } while (0)
#define CHECK_TOK(tok, exp)                                     \
    do {                                                        \
        if (tok.type() != exp) {                                \
            DEBUG("CHECK_TOK " << __FILE__ << ":" << __LINE__); \
            parseErrorUnexpected(lex, tok, Token(exp));   \
        }                                                       \
    } while (0)

// --- path.cpp
enum eParsePathGenericMode {
    PATH_GENERIC_NONE,
    PATH_GENERIC_EXPR,
    PATH_GENERIC_TYPE
};

extern ASTPath ParsePath(TokenStream& lex, eParsePathGenericMode genericMode); // Auto-determines
extern ASTPath ParsePath(TokenStream& lex, bool isAbs, eParsePathGenericMode genericMode);
extern ::std::vector<ASTPathNode> ParsePathNodes(TokenStream& lex, eParsePathGenericMode genericMode);
extern ASTPathParams ParsePathGenericList(TokenStream& lex);

extern ASTVisibility ParsePublicity(TokenStream& lex, bool allowRestricted = true);
extern ASTHigherRankedBounds ParseHRB(TokenStream& lex);
extern ASTHigherRankedBounds ParseHRBOpt(TokenStream& lex);
extern ASTAttributeList ParseItemAttrs(TokenStream& lex);
extern void ParseParentAttrs(TokenStream& lex, ASTAttributeList& out);
extern ASTAttribute ParseMetaItem(TokenStream& lex);
extern ASTMacroInvocation ParseMacroInvocation(ProtoSpan ps, ASTPath path, TokenStream& lex);
extern ASTType* ParseType(TokenStream& lex, bool allowTraitList = true);
enum class AllowOrPattern {
    No,
    Yes,
};
extern ASTPattern ParsePattern(TokenStream& lex, AllowOrPattern allowOr = AllowOrPattern::Yes);

extern void ParseImplItem(TokenStream& lex, ASTImpl& impl);
extern ASTNamed<ASTItem> ParseTraitItem(TokenStream& lex);
extern void ParseModItem(TokenStream& lex, ASTModule& mod, ASTAttributeList metaItems);
extern ASTNamed<ASTItem> ParseModItemS(TokenStream& lex, const ASTModule::FileInfo& modFileinfo, const ASTAbsolutePath& modPath, ASTAttributeList metaItems);
extern void ParseModRootItems(TokenStream& lex, ASTModule& mod);
extern ASTNamed<ASTItem> ParseExternBlockItem(TokenStream& lex, const std::string& abi);

extern ASTExpr ParseExpr(TokenStream& lex);
extern ASTExpr ParseExprBlock(TokenStream& lex);
extern ASTExprNodeP ParseExpr0(TokenStream& lex);
extern ASTExprNodeP ParseExpr13(TokenStream& lex); // Unaries
extern ASTExprNodeP ParseExprVal(TokenStream& lex);
extern ASTExprNodeP ParseExprBlockNode(TokenStream& lex);
extern ASTExprNodeP ParseExprBlockLine(TokenStream& lex, bool* addSilence);
extern ASTExprNodeP ParseExprBlockLineWithItems(TokenStream& lex, ::std::shared_ptr<ASTModule>& localMod, bool& addSilenceIfEnd);
extern ASTExprNodeP ParseStmt(TokenStream& lex);

// unwrapped = Exclude the enclosing brackets (used by macro parse code)
extern TokenTree ParseTT(TokenStream& lex, bool unwrapped);

extern bool ParseIsTokValue(eTokenType tokType);
