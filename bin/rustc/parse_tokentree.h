#pragma once

#include "parse_token.h"
#include "ident.h"
#include <vector>

namespace AST {
    enum class Edition;
}

class TokenTree {
    AST::Edition edition = (AST::Edition)0; // 2015
    Ident::Hygiene mHygiene;
    Token mTok;
    ::std::vector<TokenTree> subtrees;

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
        return mTok.type() != TOK_NULL;
    }

    size_t size() const {
        return subtrees.size();
    }

    const TokenTree& operator[](unsigned int idx) const;

    TokenTree& operator[](unsigned int idx);

    const Token& tok() const {
        return mTok;
    }

    Token& tok() {
        return mTok;
    }

    const Ident::Hygiene& hygiene() const {
        return mHygiene;
    }

    const AST::Edition& get_edition() const {
        return edition;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const TokenTree& tt);
};
