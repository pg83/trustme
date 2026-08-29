#pragma once

#include "ast_ast.h"
#include "parse_tokenstream.h"


#define GET_TOK(tok, lex) ((tok = lex.getToken()).type())
#define PUTBACK(tok, lex) lex.putback(std::move(tok))
#define LOOK_AHEAD(lex) (lex.lookahead(0))
#define GET_CHECK_TOK(tok, lex, exp)                    \
    do {                                                \
        if ((tok = lex.getToken()).type() != exp) {     \
            parseErrorUnexpected(lex, tok, Token(exp)); \
        }                                               \
    } while (0)
#define CHECK_TOK(tok, exp)                             \
    do {                                                \
        if (tok.type() != exp) {                        \
            parseErrorUnexpected(lex, tok, Token(exp)); \
        }                                               \
    } while (0)

enum eParsePathGenericMode {
    PATH_GENERIC_NONE,
    PATH_GENERIC_EXPR,
    PATH_GENERIC_TYPE
};

ASTPath ParsePath(TokenStream& lex, eParsePathGenericMode genericMode);
ASTPath ParsePath(TokenStream& lex, bool isAbs, eParsePathGenericMode genericMode);
std::vector<ASTPathNode> ParsePathNodes(TokenStream& lex, eParsePathGenericMode genericMode);
ASTPathParams ParsePathGenericList(TokenStream& lex);

ASTVisibility ParsePublicity(TokenStream& lex, bool allowRestricted = true);
ASTHigherRankedBounds ParseHRB(TokenStream& lex);
ASTHigherRankedBounds ParseHRBOpt(TokenStream& lex);
ASTAttributeList ParseItemAttrs(TokenStream& lex);
void ParseParentAttrs(TokenStream& lex, ASTAttributeList& out);
ASTAttribute ParseMetaItem(TokenStream& lex);
ASTMacroInvocation ParseMacroInvocation(ProtoSpan ps, ASTPath path, TokenStream& lex);
ASTType* ParseType(TokenStream& lex, bool allowTraitList = true);
enum class AllowOrPattern {
    No,
    Yes,
};
ASTPattern ParsePattern(TokenStream& lex, AllowOrPattern allowOr = AllowOrPattern::Yes);

void ParseImplItem(TokenStream& lex, ASTImpl& impl);
ASTNamed<ASTItem> ParseTraitItem(TokenStream& lex);
void ParseModItem(TokenStream& lex, ASTModule& mod, ASTAttributeList metaItems);
ASTNamed<ASTItem> ParseModItemS(TokenStream& lex, const ASTModule::FileInfo& modFileinfo, const ASTAbsolutePath& modPath, ASTAttributeList metaItems);
void ParseModRootItems(TokenStream& lex, ASTModule& mod);
ASTNamed<ASTItem> ParseExternBlockItem(TokenStream& lex, const std::string& abi);

ASTExpr ParseExpr(TokenStream& lex);
ASTExpr ParseExprBlock(TokenStream& lex);
ASTExprNodeP ParseExpr0(TokenStream& lex);
ASTExprNodeP ParseExpr13(TokenStream& lex);
ASTExprNodeP ParseExprVal(TokenStream& lex);
ASTExprNodeP ParseExprBlockNode(TokenStream& lex);
ASTExprNodeP ParseExprBlockLine(TokenStream& lex, bool* addSilence);
ASTExprNodeP ParseExprBlockLineWithItems(TokenStream& lex, std::shared_ptr<ASTModule>& localMod, bool& addSilenceIfEnd);
ASTExprNodeP ParseStmt(TokenStream& lex);

TokenTree ParseTT(TokenStream& lex, bool unwrapped);

bool ParseIsTokValue(eTokenType tokType);
