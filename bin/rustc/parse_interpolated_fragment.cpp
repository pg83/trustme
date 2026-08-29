#include "parse_interpolated_fragment.h"

#include "output.h"
#include "ast_ast.h"
#include "ast_expr.h"

using namespace stl;

InterpolatedFragment::~InterpolatedFragment() {
    if (ptr) {
        switch (type) {
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
                delete reinterpret_cast<ASTType**>(ptr);
                break;
            case InterpolatedFragment::EXPR:
            case InterpolatedFragment::STMT:
            case InterpolatedFragment::BLOCK:
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
    : type(x.type)
    , span(std::move(x.span))
{
    ptr = x.ptr, x.ptr = nullptr;
}

InterpolatedFragment& InterpolatedFragment::operator=(InterpolatedFragment&& x) {
    type = x.type;
    ptr = x.ptr, x.ptr = nullptr;
    span = std::move(x.span);
    return *this;
}

InterpolatedFragment::InterpolatedFragment(InterpolatedFragment::Type type, ASTExprNode* ptr)
    : type(type)
    , ptr(ptr)
{
}

InterpolatedFragment::InterpolatedFragment(ASTAttribute v)
    : type(InterpolatedFragment::META)
    , ptr(new ASTAttribute(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(ASTNamed<ASTItem> v)
    : type(InterpolatedFragment::ITEM)
    , ptr(new ASTNamed<ASTItem>(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(InterpolatedFragment::Type type, ASTNamed<ASTItem> v)
    : type(type)
    , ptr(new ASTNamed<ASTItem>(mv$(v)))
{
    BUG_ASSERT(type == InterpolatedFragment::STMT_ITEM || type == InterpolatedFragment::ITEM);
}

InterpolatedFragment::InterpolatedFragment(TokenTree v)
    : type(InterpolatedFragment::TT)
    , ptr(new TokenTree(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(ASTPath v, Span span)
    : type(InterpolatedFragment::PATH)
    , ptr(new ASTPath(mv$(v)))
    , span(std::move(span))
{
}

InterpolatedFragment::InterpolatedFragment(ASTPattern v)
    : type(InterpolatedFragment::PAT)
    , ptr(new ASTPattern(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(ASTType* v)
    : type(InterpolatedFragment::TYPE)
    , ptr(new ASTType*(mv$(v)))
{
}

InterpolatedFragment::InterpolatedFragment(ASTVisibility v)
    : type(InterpolatedFragment::VIS)
    , ptr(new ASTVisibility(mv$(v)))
{
}

TokenTree& InterpolatedFragment::asTt() {
    BUG_ASSERT(type == TT);
    return *reinterpret_cast<TokenTree*>(ptr);
}

const TokenTree& InterpolatedFragment::asTt() const {
    BUG_ASSERT(type == TT);
    return *reinterpret_cast<TokenTree*>(ptr);
}

template <>
void stl::output<ZeroCopyOutput, InterpolatedFragment>(ZeroCopyOutput& os, InterpolatedFragment const& x) {
    switch (x.type) {
        case InterpolatedFragment::TT:
            os << StringView("tt[") << x.asTt() << StringView("]");
            break;
        case InterpolatedFragment::PAT:
            os << StringView("pat[") << *reinterpret_cast<ASTPattern*>(x.ptr) << StringView("]");
            break;
        case InterpolatedFragment::PATH:
            os << StringView("path[") << *reinterpret_cast<ASTPath*>(x.ptr) << StringView("]");
            break;
        case InterpolatedFragment::TYPE:
            os << StringView("type[") << *reinterpret_cast<ASTType**>(x.ptr) << StringView("]");
            break;

        case InterpolatedFragment::EXPR:
            os << StringView("expr[") << *reinterpret_cast<const ASTExprNode*>(x.ptr) << StringView("]");
            break;
        case InterpolatedFragment::STMT:
            os << StringView("stmt[") << *reinterpret_cast<const ASTExprNode*>(x.ptr) << StringView("]");
            break;
        case InterpolatedFragment::STMT_ITEM: {
            const auto& namedItem = *reinterpret_cast<const ASTNamed<ASTItem>*>(x.ptr);
            os << StringView("stmt-item[") << namedItem.data.tagStr() << StringView("(") << namedItem.name << StringView(")]");
        } break;
        case InterpolatedFragment::BLOCK:
            os << StringView("block[") << *reinterpret_cast<const ASTExprNode*>(x.ptr) << StringView("]");
            break;

        case InterpolatedFragment::META:
            os << StringView("meta[") << *reinterpret_cast<const ASTAttribute*>(x.ptr) << StringView("]");
            break;
        case InterpolatedFragment::ITEM: {
            const auto& namedItem = *reinterpret_cast<const ASTNamed<ASTItem>*>(x.ptr);
            os << StringView("item[") << namedItem.data.tagStr() << StringView("(") << namedItem.name << StringView(")]");
        } break;
        case InterpolatedFragment::VIS:
            os << StringView("vis[") << *reinterpret_cast<const ASTVisibility*>(x.ptr) << StringView("]");
            break;
    }
    return;
}
