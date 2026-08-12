#include "parse_interpolated_fragment.h"
#include <iostream>
#include "ast_ast.h"
#include "ast_expr.h" // For definition of ExprNode

InterpolatedFragment::~InterpolatedFragment() {
    if (ptr) {
        switch (mType) {
            case InterpolatedFragment::TT:
                delete reinterpret_cast<TokenTree*>(ptr);
                break;
            case InterpolatedFragment::PAT:
                delete reinterpret_cast<ASTPattern*>(ptr);
                break;
            case InterpolatedFragment::PATH:
                delete reinterpret_cast<ASTPath*>(ptr);
                break;
            case InterpolatedFragment::TYPE:
                delete reinterpret_cast<TypeRef*>(ptr);
                break;
            case InterpolatedFragment::EXPR:
            case InterpolatedFragment::STMT:
            case InterpolatedFragment::BLOCK:
                delete reinterpret_cast<ASTExprNode*>(ptr);
                break;
            case InterpolatedFragment::META:
                delete reinterpret_cast<ASTAttribute*>(ptr);
                break;
            case InterpolatedFragment::STMT_ITEM:
            case InterpolatedFragment::ITEM:
                delete reinterpret_cast<ASTNamed<ASTItem>*>(ptr);
                break;
            case InterpolatedFragment::VIS:
                delete reinterpret_cast<ASTVisibility*>(ptr);
                break;
        }
    }
}

InterpolatedFragment::InterpolatedFragment(InterpolatedFragment&& x)
    : mType(x.mType)
{
    ptr = x.ptr, x.ptr = nullptr;
}

InterpolatedFragment& InterpolatedFragment::operator=(InterpolatedFragment&& x) {
    mType = x.mType;
    ptr = x.ptr, x.ptr = nullptr;
    return *this;
}

InterpolatedFragment::InterpolatedFragment(InterpolatedFragment::Type type, ASTExprNode* ptr)
    : mType(type)
    , ptr(ptr)
{
}

InterpolatedFragment::InterpolatedFragment(ASTAttribute v)
    : mType(InterpolatedFragment::META)
    , ptr(new ASTAttribute(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(ASTNamed<ASTItem> v)
    : mType(InterpolatedFragment::ITEM)
    , ptr(new ASTNamed<ASTItem>(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(InterpolatedFragment::Type type, ASTNamed<ASTItem> v)
    : mType(type)
    , ptr(new ASTNamed<ASTItem>(mv$(v)))
{
    assert(type == InterpolatedFragment::STMT_ITEM || type == InterpolatedFragment::ITEM);
}

InterpolatedFragment::InterpolatedFragment(TokenTree v)
    : mType(InterpolatedFragment::TT)
    , ptr(new TokenTree(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(ASTPath v)
    : mType(InterpolatedFragment::PATH)
    , ptr(new ASTPath(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(ASTPattern v)
    : mType(InterpolatedFragment::PAT)
    , ptr(new ASTPattern(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(TypeRef v)
    : mType(InterpolatedFragment::TYPE)
    , ptr(new TypeRef(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(ASTVisibility v)
    : mType(InterpolatedFragment::VIS)
    , ptr(new ASTVisibility(mv$(v)))
{
}

::std::ostream& operator<<(::std::ostream& os, InterpolatedFragment const& x) {
    switch (x.mType) {
        case InterpolatedFragment::TT:
            os << "tt[" << x.asTt() << "]";
            break;
        case InterpolatedFragment::PAT:
            os << "pat[" << *reinterpret_cast<ASTPattern*>(x.ptr) << "]";
            break;
        case InterpolatedFragment::PATH:
            os << "path[" << *reinterpret_cast<ASTPath*>(x.ptr) << "]";
            break;
        case InterpolatedFragment::TYPE:
            os << "type[" << *reinterpret_cast<TypeRef*>(x.ptr) << "]";
            break;

        case InterpolatedFragment::EXPR:
            os << "expr[" << *reinterpret_cast<const ASTExprNode*>(x.ptr) << "]";
            break;
        case InterpolatedFragment::STMT:
            os << "stmt[" << *reinterpret_cast<const ASTExprNode*>(x.ptr) << "]";
            break;
        case InterpolatedFragment::STMT_ITEM: {
            const auto& namedItem = *reinterpret_cast<const ASTNamed<ASTItem>*>(x.ptr);
            os << "stmt-item[" << namedItem.data.tagStr() << "(" << namedItem.name << ")]";
        } break;
        case InterpolatedFragment::BLOCK:
            os << "block[" << *reinterpret_cast<const ASTExprNode*>(x.ptr) << "]";
            break;

        case InterpolatedFragment::META:
            os << "meta[" << *reinterpret_cast<const ASTAttribute*>(x.ptr) << "]";
            break;
        case InterpolatedFragment::ITEM: {
            const auto& namedItem = *reinterpret_cast<const ASTNamed<ASTItem>*>(x.ptr);
            os << "item[" << namedItem.data.tagStr() << "(" << namedItem.name << ")]";
        } break;
        case InterpolatedFragment::VIS:
            os << "vis[" << *reinterpret_cast<const ASTVisibility*>(x.ptr) << "]";
            break;
    }
    return os;
}

// :vis

TokenTree& InterpolatedFragment::asTt() {
    assert(mType == TT);
    return *reinterpret_cast<TokenTree*>(ptr);
}
const TokenTree& InterpolatedFragment::asTt() const {
    assert(mType == TT);
    return *reinterpret_cast<TokenTree*>(ptr);
}
