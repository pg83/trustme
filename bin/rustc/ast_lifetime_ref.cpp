#include "ast_lifetime_ref.h"

ASTLifetimeRef::ASTLifetimeRef(Ident name, uint32_t binding)
    : name_(::std::move(name))
    , binding_(binding)
{
}

ASTLifetimeRef::ASTLifetimeRef()
    : ASTLifetimeRef("", BINDING_UNSPECIFIED)
{
}

ASTLifetimeRef::ASTLifetimeRef(Ident name)
    : ASTLifetimeRef(::std::move(name), BINDING_UNBOUND)
{
}

void ASTLifetimeRef::setBinding(uint16_t b) {
    assert(binding_ == BINDING_UNBOUND);
    binding_ = b;
}
