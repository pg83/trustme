#pragma once

#include "parse_tokentree.h"
#include "parse_tokenstream.h"

class TTStream: public TokenStream {
    std::vector<std::pair<unsigned int, const TokenTree*>> stack;
    Span parentSpan;
    ASTEdition edition = ASTEdition::Rust2015;
    const Ident::Hygiene* hygienePtr = nullptr;

public:
    TTStream(Span parent, ParseState ps, const TokenTree& inputTt);
    ~TTStream();

    Position getPosition() const override;

    Span outerSpan() const override;

protected:
    ASTEdition realGetEdition() const override;
    Ident::Hygiene realGetHygiene() const override;
    Token realGetToken() override;
};

class TTStreamO: public TokenStream {
    Span parentSpan;
    Position lastPos;
    TokenTree inputTt;
    std::vector<std::pair<unsigned int, TokenTree*>> stack;
    ASTEdition edition = ASTEdition::Rust2015;
    const Ident::Hygiene* hygienePtr = nullptr;

public:
    TTStreamO(Span parent, ParseState ps, TokenTree inputTt);
    ~TTStreamO();

    TTStreamO(TTStreamO&& x) = default;
    TTStreamO& operator=(TTStreamO&& x) = default;

    Position getPosition() const override;

    Span outerSpan() const override;

protected:
    ASTEdition realGetEdition() const override;
    Ident::Hygiene realGetHygiene() const override;
    Token realGetToken() override;
};
