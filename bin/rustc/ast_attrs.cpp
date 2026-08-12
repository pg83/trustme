#include "ast_attrs.h"

namespace AST {

Attribute::Attribute(Span sp, AttributeName name, TokenTree data)
    : m_span(::std::move(sp))
    , m_name(::std::move(name))
    , m_data(::std::move(data))
    , m_is_inert(false) {
}
}

namespace AST {

::std::ostream& operator<<(::std::ostream& os, const Attribute& x) {
    x.fmt(os);
    return os;
}
}
