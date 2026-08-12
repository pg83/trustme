#include "ast_lifetime_ref.h"

namespace AST {

LifetimeRef::LifetimeRef(Ident name, uint32_t binding)
    : mName(::std::move(name))
    , mBinding(binding) {
}
LifetimeRef::LifetimeRef()
    : LifetimeRef("", BINDING_UNSPECIFIED) {
}
LifetimeRef::LifetimeRef(Ident name)
    : LifetimeRef(::std::move(name), BINDING_UNBOUND) {
}
void LifetimeRef::setBinding(uint16_t b) {
    assert(mBinding == BINDING_UNBOUND);
    mBinding = b;
}
}
