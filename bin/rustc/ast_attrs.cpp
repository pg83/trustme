#include "ast_attrs.h"

ASTAttribute::ASTAttribute(Span sp, ASTAttributeName name, TokenTree data)
    : mSpan(::std::move(sp))
    , mName(::std::move(name))
    , mData(::std::move(data))
    , mIsInert(false)
{
}

::std::ostream& operator<<(::std::ostream& os, const ASTAttribute& x) {
    x.fmt(os);
    return os;
}
