#pragma once

#include "span.h"

#include <iosfwd>
#include <cassert>

struct ASTType;
class TokenTree;
struct Ident;

class ASTVisibility;
class ASTPattern;
class ASTPath;
class ASTExprNode;
class ASTAttribute;
template <typename T>
struct ASTNamed;
class ASTItem;

class InterpolatedFragment {
public:
    enum Type {
        TT,
        PAT,
        PATH,
        TYPE,

        EXPR,
        STMT,
        STMT_ITEM,
        BLOCK,

        META,
        ITEM,
        VIS,
    } type;

    // Owned type-pruned pointer
    void* ptr;
    Span span;

    InterpolatedFragment(InterpolatedFragment&&);
    InterpolatedFragment& operator=(InterpolatedFragment&&);

    InterpolatedFragment(TokenTree);
    InterpolatedFragment(ASTPattern);
    InterpolatedFragment(ASTPath, Span span = {});
    InterpolatedFragment(::ASTType*);
    InterpolatedFragment(ASTAttribute);
    InterpolatedFragment(ASTNamed<ASTItem>);
    InterpolatedFragment(Type, ASTNamed<ASTItem>);
    ~InterpolatedFragment();
    InterpolatedFragment(Type, ASTExprNode*);
    InterpolatedFragment(ASTVisibility); // :vis

    TokenTree& asTt();

    const TokenTree& asTt() const;

    friend ::std::ostream& operator<<(::std::ostream& os, const InterpolatedFragment& x);
};
