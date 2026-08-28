#include "parse_tokentree.h"

#include "common.h"
#include "ast_edition.h"

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

std::ostream& operator<<(std::ostream& os, const TokenTree& tt) {
    if (tt.subtrees.size() == 0) {
        switch (tt.tok_.type()) {
            case TOK_IDENT:
            case TOK_LIFETIME:
                os << "/*" << tt.edition << " " << tt.hygiene_ << "*/";
                break;
            default:
                if (TOK_INTERPOLATED_PATH <= tt.tok_.type() && tt.tok_.type() <= TOK_INTERPOLATED_VIS) {
                    os << "/*" << tt.edition << " int*/";
                } else {
                    os << "/*" << tt.edition << "*/";
                }
                break;
        }
        return os << tt.tok_.toStr();
    } else {
        os << "/*" << tt.edition << " " << tt.hygiene_ << " TT*/";
        bool first = true;
        for (const auto& i : tt.subtrees) {
            if (!first) {
                os << " ";
            }
            os << i;
            first = false;
        }
        return os;
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
