#include "ident.h"

#include <cassert>
#include <set>

int main() {
    auto definitionContext = Ident::Hygiene::newScope();
    auto expansionContext = Ident::Hygiene::newScopeChained(definitionContext);

    Ident definition(definitionContext, RcString::newInterned("name"));
    Ident expansion(expansionContext, RcString::newInterned("name"));

    const bool orderingEqual = !(definition < expansion) && !(expansion < definition);
    assert((definition == expansion) == orderingEqual);

    std::set<Ident> names;
    names.insert(definition);
    names.insert(expansion);
    assert(names.size() == 2);
}
