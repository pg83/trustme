#include "ast_lifetime_ref.h"


ASTLifetimeRef::ASTLifetimeRef(Ident name, uint32_t binding)
    : mName(::std::move(name))
    , mBinding(binding) {
}
ASTLifetimeRef::ASTLifetimeRef()
    : ASTLifetimeRef("", BINDING_UNSPECIFIED) {
}
ASTLifetimeRef::ASTLifetimeRef(Ident name)
    : ASTLifetimeRef(::std::move(name), BINDING_UNBOUND) {
}
void ASTLifetimeRef::setBinding(uint16_t b) {
    assert(mBinding == BINDING_UNBOUND);
    mBinding = b;
}
