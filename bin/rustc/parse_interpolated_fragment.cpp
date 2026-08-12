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
                delete reinterpret_cast<AST::Pattern*>(ptr);
                break;
            case InterpolatedFragment::PATH:
                delete reinterpret_cast<AST::Path*>(ptr);
                break;
            case InterpolatedFragment::TYPE:
                delete reinterpret_cast<TypeRef*>(ptr);
                break;
            case InterpolatedFragment::EXPR:
            case InterpolatedFragment::STMT:
            case InterpolatedFragment::BLOCK:
                delete reinterpret_cast<AST::ExprNode*>(ptr);
                break;
            case InterpolatedFragment::META:
                delete reinterpret_cast<AST::Attribute*>(ptr);
                break;
            case InterpolatedFragment::STMT_ITEM:
            case InterpolatedFragment::ITEM:
                delete reinterpret_cast<AST::Named<AST::Item>*>(ptr);
                break;
            case InterpolatedFragment::VIS:
                delete reinterpret_cast<AST::Visibility*>(ptr);
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

InterpolatedFragment::InterpolatedFragment(InterpolatedFragment::Type type, AST::ExprNode* ptr)
    : mType(type)
    , ptr(ptr)
{
}

InterpolatedFragment::InterpolatedFragment(AST::Attribute v)
    : mType(InterpolatedFragment::META)
    , ptr(new AST::Attribute(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(::AST::Named<::AST::Item> v)
    : mType(InterpolatedFragment::ITEM)
    , ptr(new ::AST::Named<::AST::Item>(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(InterpolatedFragment::Type type, ::AST::Named<::AST::Item> v)
    : mType(type)
    , ptr(new ::AST::Named<::AST::Item>(mv$(v)))
{
    assert(type == InterpolatedFragment::STMT_ITEM || type == InterpolatedFragment::ITEM);
}

InterpolatedFragment::InterpolatedFragment(TokenTree v)
    : mType(InterpolatedFragment::TT)
    , ptr(new TokenTree(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(AST::Path v)
    : mType(InterpolatedFragment::PATH)
    , ptr(new AST::Path(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(AST::Pattern v)
    : mType(InterpolatedFragment::PAT)
    , ptr(new AST::Pattern(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(TypeRef v)
    : mType(InterpolatedFragment::TYPE)
    , ptr(new TypeRef(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(AST::Visibility v)
    : mType(InterpolatedFragment::VIS)
    , ptr(new AST::Visibility(mv$(v)))
{
}

::std::ostream& operator<<(::std::ostream& os, InterpolatedFragment const& x) {
    switch (x.mType) {
        case InterpolatedFragment::TT:
            os << "tt[" << x.asTt() << "]";
            break;
        case InterpolatedFragment::PAT:
            os << "pat[" << *reinterpret_cast<AST::Pattern*>(x.ptr) << "]";
            break;
        case InterpolatedFragment::PATH:
            os << "path[" << *reinterpret_cast<AST::Path*>(x.ptr) << "]";
            break;
        case InterpolatedFragment::TYPE:
            os << "type[" << *reinterpret_cast<TypeRef*>(x.ptr) << "]";
            break;

        case InterpolatedFragment::EXPR:
            os << "expr[" << *reinterpret_cast<const AST::ExprNode*>(x.ptr) << "]";
            break;
        case InterpolatedFragment::STMT:
            os << "stmt[" << *reinterpret_cast<const AST::ExprNode*>(x.ptr) << "]";
            break;
        case InterpolatedFragment::STMT_ITEM: {
            const auto& namedItem = *reinterpret_cast<const AST::Named<AST::Item>*>(x.ptr);
            os << "stmt-item[" << namedItem.data.tag_str() << "(" << namedItem.name << ")]";
        } break;
        case InterpolatedFragment::BLOCK:
            os << "block[" << *reinterpret_cast<const AST::ExprNode*>(x.ptr) << "]";
            break;

        case InterpolatedFragment::META:
            os << "meta[" << *reinterpret_cast<const AST::Attribute*>(x.ptr) << "]";
            break;
        case InterpolatedFragment::ITEM: {
            const auto& namedItem = *reinterpret_cast<const AST::Named<AST::Item>*>(x.ptr);
            os << "item[" << namedItem.data.tag_str() << "(" << namedItem.name << ")]";
        } break;
        case InterpolatedFragment::VIS:
            os << "vis[" << *reinterpret_cast<const AST::Visibility*>(x.ptr) << "]";
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
