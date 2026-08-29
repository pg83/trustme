#include "parse_tokentree.h"

#include "common.h"
#include "output.h"
#include "ast_edition.h"

using namespace stl;

TokenTree TokenTree::clone() const {
    if (subtrees.size() == 0) {
        return TokenTree(edition, hygiene_, tok_.clone());
    } else {
        std::vector<TokenTree> ents;
        ents.reserve(subtrees.size());
        for (const auto& sub : subtrees) {
            ents.push_back(sub.clone());
        }
        return TokenTree(edition, hygiene_, mv$(ents));
    }
}

TokenTree::~TokenTree() {
}

TokenTree::TokenTree() {
}

TokenTree::TokenTree(enum eTokenType ty)
    : tok_(Token(ty))
{
}

TokenTree::TokenTree(Token tok)
    : tok_(std::move(tok))
{
}

TokenTree::TokenTree(ASTEdition edition, Token tok)
    : edition(edition)
    , tok_(std::move(tok))
{
}

TokenTree::TokenTree(ASTEdition edition, Ident::Hygiene hygiene, Token tok)
    : edition(edition)
    , hygiene_(std::move(hygiene))
    , tok_(std::move(tok))
{
}

TokenTree::TokenTree(ASTEdition edition, Ident::Hygiene hygiene, std::vector<TokenTree> subtrees)
    : edition(edition)
    , hygiene_(std::move(hygiene))
    , subtrees(std::move(subtrees))
{
}

const TokenTree& TokenTree::operator[](unsigned int idx) const {
    BUG_ASSERT(idx < subtrees.size());
    return subtrees[idx];
}

TokenTree& TokenTree::operator[](unsigned int idx) {
    BUG_ASSERT(idx < subtrees.size());
    return subtrees[idx];
}

void TokenTree::fmt(ZeroCopyOutput& os) const {
    if (subtrees.empty()) {
        switch (tok_.type()) {
            case TOK_IDENT:
            case TOK_LIFETIME:
                os << StringView("/*") << edition << StringView(" ") << hygiene_ << StringView("*/");
                break;
            default:
                if (TOK_INTERPOLATED_PATH <= tok_.type() && tok_.type() <= TOK_INTERPOLATED_VIS) {
                    os << StringView("/*") << edition << StringView(" int*/");
                } else {
                    os << StringView("/*") << edition << StringView("*/");
                }
                break;
        }
        os << tok_.toStr();
    } else {
        os << StringView("/*") << edition << StringView(" ") << hygiene_ << StringView(" TT*/");
        bool first = true;
        for (const auto& i : subtrees) {
            if (!first) {
                os << StringView(" ");
            }
            os << i;
            first = false;
        }
    }
}

template <>
void stl::output<ZeroCopyOutput, TokenTree>(ZeroCopyOutput& os, const TokenTree& tree) {
    tree.fmt(os);
}
