#include "ast_attrs.h"

namespace AST {

Attribute::Attribute(Span sp, AttributeName name, TokenTree data)
    : mSpan(::std::move(sp))
    , mName(::std::move(name))
    , mData(::std::move(data))
    , isInert(false) {
}
}

namespace AST {

::std::ostream& operator<<(::std::ostream& os, const Attribute& x) {
    x.fmt(os);
    return os;
}
}
