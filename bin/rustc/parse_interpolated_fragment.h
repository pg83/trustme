#pragma once

#include <cassert>
#include <iosfwd>

class TypeRef;
class TokenTree;
struct Ident;

namespace AST {
    class Visibility;
    class Pattern;
    class Path;
    class ExprNode;
    class Attribute;
    template <typename T>
    struct Named;
    class Item;
};

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
    InterpolatedFragment(::AST::Pattern);
    InterpolatedFragment(::AST::Path);
    InterpolatedFragment(::TypeRef);
    InterpolatedFragment(::AST::Attribute);
    InterpolatedFragment(::AST::Named<AST::Item>);
    InterpolatedFragment(Type, ::AST::Named<AST::Item>);
    ~InterpolatedFragment();
    InterpolatedFragment(Type, ::AST::ExprNode*);
    InterpolatedFragment(AST::Visibility); // :vis

    TokenTree& asTt();

    const TokenTree& asTt() const;

    friend ::std::ostream& operator<<(::std::ostream& os, const InterpolatedFragment& x);
};
