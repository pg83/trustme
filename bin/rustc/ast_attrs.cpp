#include "ast_attrs.h"

ASTAttribute::ASTAttribute(Span sp, ASTAttributeName name, TokenTree data)
    : span_(::std::move(sp))
    , name_(::std::move(name))
    , data_(::std::move(data))
    , isInert_(false)
{
}

::std::ostream& operator<<(::std::ostream& os, const ASTAttribute& x) {
    x.fmt(os);
    return os;
}
