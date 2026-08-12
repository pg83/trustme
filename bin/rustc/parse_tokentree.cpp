#include "parse_tokentree.h"
#include "ast_edition.h"
#include "common.h"

TokenTree TokenTree::clone() const {
    if (subtrees.size() == 0) {
        return TokenTree(edition, mHygiene, mTok.clone());
    } else {
        ::std::vector<TokenTree> ents;
        ents.reserve(subtrees.size());
        for (const auto& sub : subtrees) {
            ents.push_back(sub.clone());
        }
        return TokenTree(edition, mHygiene, mv$(ents));
    }
}

::std::ostream& operator<<(::std::ostream& os, const TokenTree& tt) {
    if (tt.subtrees.size() == 0) {
        switch (tt.mTok.type()) {
            case TOK_IDENT:
            case TOK_LIFETIME:
                os << "/*" << tt.edition << " " << tt.mHygiene << "*/";
                break;
            default:
                if (TOK_INTERPOLATED_PATH <= tt.mTok.type() && tt.mTok.type() <= TOK_INTERPOLATED_VIS) {
                    os << "/*" << tt.edition << " int*/";
                } else {
                    os << "/*" << tt.edition << "*/";
                }
                break;
        }
        return os << tt.mTok.toStr();
    } else {
        os << "/*" << tt.edition << " " << tt.mHygiene << " TT*/";
        // NOTE: All TTs (except the outer tt on a macro invocation) include the grouping
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
    : mTok(Token(ty)) {
}
TokenTree::TokenTree(Token tok)
    : mTok(::std::move(tok)) {
}
TokenTree::TokenTree(AST::Edition edition, Token tok)
    : edition(edition)
    , mTok(::std::move(tok)) {
}
TokenTree::TokenTree(AST::Edition edition, Ident::Hygiene hygiene, Token tok)
    : edition(edition)
    , mHygiene(::std::move(hygiene))
    , mTok(::std::move(tok)) {
}
TokenTree::TokenTree(AST::Edition edition, Ident::Hygiene hygiene, ::std::vector<TokenTree> subtrees)
    : edition(edition)
    , mHygiene(::std::move(hygiene))
    , subtrees(::std::move(subtrees)) {
}
const TokenTree& TokenTree::operator[](unsigned int idx) const {
    assert(idx < subtrees.size());
    return subtrees[idx];
}
TokenTree& TokenTree::operator[](unsigned int idx) {
    assert(idx < subtrees.size());
    return subtrees[idx];
}
