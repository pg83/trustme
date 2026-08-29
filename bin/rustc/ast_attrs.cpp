#include "ast_attrs.h"
#include "output.h"

using namespace stl;

ASTAttribute::ASTAttribute(Span sp, ASTAttributeName name, TokenTree data)
    : span_(std::move(sp))
    , name_(std::move(name))
    , data_(std::move(data))
    , isInert_(false)
{
}

namespace stl {
template <>
void output<ZeroCopyOutput, ASTAttribute>(ZeroCopyOutput& os, const ASTAttribute& x) {
    x.fmt(os);
    return;
}
}
