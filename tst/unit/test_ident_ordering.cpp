#include "ident.h"

#include <cassert>
#include <set>

int main() {
    auto definitionContext = Ident::Hygiene::new_scope();
    auto expansionContext = Ident::Hygiene::new_scope_chained(definitionContext);

    Ident definition(definitionContext, RcString::new_interned("name"));
    Ident expansion(expansionContext, RcString::new_interned("name"));

    const bool ordering_equal = !(definition < expansion) && !(expansion < definition);
    assert((definition == expansion) == ordering_equal);

    std::set<Ident> names;
    names.insert(definition);
    names.insert(expansion);
    assert(names.size() == 2);
}
