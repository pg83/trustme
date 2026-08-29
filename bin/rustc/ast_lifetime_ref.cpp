#include "ast_lifetime_ref.h"

#include "output.h"

using namespace stl;

ASTLifetimeRef::ASTLifetimeRef(Ident name, u32 binding)
    : name_(std::move(name))
    , binding_(binding)
{
}

ASTLifetimeRef::ASTLifetimeRef()
    : ASTLifetimeRef("", BINDING_UNSPECIFIED)
{
}

ASTLifetimeRef::ASTLifetimeRef(Ident name)
    : ASTLifetimeRef(std::move(name), BINDING_UNBOUND)
{
}

void ASTLifetimeRef::setBinding(u16 b) {
    BUG_ASSERT(binding_ == BINDING_UNBOUND);
    binding_ = b;
}

namespace stl {
    template <>
    void output<ZeroCopyOutput, ASTLifetimeRef>(ZeroCopyOutput& out, ASTLifetimeRef value) {
        if (value.binding() == ASTLifetimeRef::BINDING_STATIC) {
            out << StringView("'static");
        } else if (value.binding() == ASTLifetimeRef::BINDING_INFER) {
            out << StringView("'_");
        } else if (value.binding() == ASTLifetimeRef::BINDING_UNSPECIFIED) {
            out << StringView("/*'UNSPEC*/");
        } else {
            out << StringView("'") << value.name().name;
            if (value.binding() != ASTLifetimeRef::BINDING_UNBOUND) {
                out << StringView("/*") << value.binding() << StringView("*/");
            }
        }
    }
}
