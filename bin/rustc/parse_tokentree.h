#pragma once

#include "output.h"

#include "ident.h"
#include "parse_token.h"

#include <vector>

enum class ASTEdition;

class TokenTree {
    ASTEdition edition = (ASTEdition)0;
    Ident::Hygiene hygiene_;
    Token tok_;
    std::vector<TokenTree> subtrees;

public:
    virtual ~TokenTree();

    TokenTree();

    TokenTree(TokenTree&&) = default;
    TokenTree& operator=(TokenTree&&) = default;

    TokenTree(enum eTokenType ty);

    TokenTree(Token tok);

    TokenTree(ASTEdition edition, Token tok);

    TokenTree(ASTEdition edition, Ident::Hygiene hygiene, Token tok);

    TokenTree(ASTEdition edition, Ident::Hygiene hygiene, std::vector<TokenTree> subtrees);

    TokenTree clone() const;

    bool isToken() const {
        return tok_.type() != TOK_NULL;
    }

    size_t size() const {
        return subtrees.size();
    }

    const TokenTree& operator[](unsigned int idx) const;

    TokenTree& operator[](unsigned int idx);

    const Token& tok() const {
        return tok_;
    }

    Token& tok() {
        return tok_;
    }

    const Ident::Hygiene& hygiene() const {
        return hygiene_;
    }

    const ASTEdition& getEdition() const {
        return edition;
    }

    void fmt(stl::ZeroCopyOutput& out) const;
};
