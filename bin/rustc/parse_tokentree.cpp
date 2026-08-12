#include "parse_tokentree.h"
#include "ast_edition.h"
#include "common.h"

TokenTree TokenTree::clone() const {
    if (m_subtrees.size() == 0) {
        return TokenTree(m_edition, m_hygiene, m_tok.clone());
    } else {
        ::std::vector<TokenTree> ents;
        ents.reserve(m_subtrees.size());
        for (const auto& sub : m_subtrees) {
            ents.push_back(sub.clone());
        }
        return TokenTree(m_edition, m_hygiene, mv$(ents));
    }
}

::std::ostream& operator<<(::std::ostream& os, const TokenTree& tt) {
    if (tt.m_subtrees.size() == 0) {
        switch (tt.m_tok.type()) {
            case TOK_IDENT:
            case TOK_LIFETIME:
                os << "/*" << tt.m_edition << " " << tt.m_hygiene << "*/";
                break;
            default:
                if (TOK_INTERPOLATED_PATH <= tt.m_tok.type() && tt.m_tok.type() <= TOK_INTERPOLATED_VIS) {
                    os << "/*" << tt.m_edition << " int*/";
                } else {
                    os << "/*" << tt.m_edition << "*/";
                }
                break;
        }
        return os << tt.m_tok.to_str();
    } else {
        os << "/*" << tt.m_edition << " " << tt.m_hygiene << " TT*/";
        // NOTE: All TTs (except the outer tt on a macro invocation) include the grouping
        bool first = true;
        for (const auto& i : tt.m_subtrees) {
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
    : m_tok(Token(ty)) {
}
TokenTree::TokenTree(Token tok)
    : m_tok(::std::move(tok)) {
}
TokenTree::TokenTree(AST::Edition edition, Token tok)
    : m_edition(edition)
    , m_tok(::std::move(tok)) {
}
TokenTree::TokenTree(AST::Edition edition, Ident::Hygiene hygiene, Token tok)
    : m_edition(edition)
    , m_hygiene(::std::move(hygiene))
    , m_tok(::std::move(tok)) {
}
TokenTree::TokenTree(AST::Edition edition, Ident::Hygiene hygiene, ::std::vector<TokenTree> subtrees)
    : m_edition(edition)
    , m_hygiene(::std::move(hygiene))
    , m_subtrees(::std::move(subtrees)) {
}
const TokenTree& TokenTree::operator[](unsigned int idx) const {
    assert(idx < m_subtrees.size());
    return m_subtrees[idx];
}
TokenTree& TokenTree::operator[](unsigned int idx) {
    assert(idx < m_subtrees.size());
    return m_subtrees[idx];
}
