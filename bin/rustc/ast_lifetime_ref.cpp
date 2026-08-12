#include "ast_lifetime_ref.h"

namespace AST {

LifetimeRef::LifetimeRef(Ident name, uint32_t binding)
    : m_name(::std::move(name))
    , m_binding(binding) {
}
LifetimeRef::LifetimeRef()
    : LifetimeRef("", BINDING_UNSPECIFIED) {
}
LifetimeRef::LifetimeRef(Ident name)
    : LifetimeRef(::std::move(name), BINDING_UNBOUND) {
}
void LifetimeRef::set_binding(uint16_t b) {
    assert(m_binding == BINDING_UNBOUND);
    m_binding = b;
}
}
