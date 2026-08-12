#pragma once

#include "parse_token.h"
#include "ident.h"
#include <vector>

namespace AST {
    enum class Edition;
}

class TokenTree {
    AST::Edition m_edition = (AST::Edition)0; // 2015
    Ident::Hygiene m_hygiene;
    Token m_tok;
    ::std::vector<TokenTree> m_subtrees;

public:
    virtual ~TokenTree();

    TokenTree();

    TokenTree(TokenTree&&) = default;
    TokenTree& operator=(TokenTree&&) = default;

    TokenTree(enum eTokenType ty);

    TokenTree(Token tok);

    TokenTree(AST::Edition edition, Token tok);

    TokenTree(AST::Edition edition, Ident::Hygiene hygiene, Token tok);

    TokenTree(AST::Edition edition, Ident::Hygiene hygiene, ::std::vector<TokenTree> subtrees);

    TokenTree clone() const;

    bool is_token() const {
        return m_tok.type() != TOK_NULL;
    }

    size_t size() const {
        return m_subtrees.size();
    }

    const TokenTree& operator[](unsigned int idx) const;

    TokenTree& operator[](unsigned int idx);

    const Token& tok() const {
        return m_tok;
    }

    Token& tok() {
        return m_tok;
    }

    const Ident::Hygiene& hygiene() const {
        return m_hygiene;
    }

    const AST::Edition& get_edition() const {
        return m_edition;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const TokenTree& tt);
};
