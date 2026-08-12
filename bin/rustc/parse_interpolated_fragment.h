#pragma once

#include <iosfwd>
#include <cassert>

class TypeRef;
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
    } mType;

    // Owned type-pruned pointer
    void* ptr;

    InterpolatedFragment(InterpolatedFragment&&);
    InterpolatedFragment& operator=(InterpolatedFragment&&);
    //InterpolatedFragment(const InterpolatedFragment& );

    InterpolatedFragment(TokenTree);
    InterpolatedFragment(ASTPattern);
    InterpolatedFragment(ASTPath);
    InterpolatedFragment(::TypeRef);
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
