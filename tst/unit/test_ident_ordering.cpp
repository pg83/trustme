#include "ident.h"

#include <cassert>
#include <set>

int main() {
    auto definition_context = Ident::Hygiene::new_scope();
    auto expansion_context = Ident::Hygiene::new_scope_chained(definition_context);

    Ident definition(definition_context, RcString::new_interned("name"));
    Ident expansion(expansion_context, RcString::new_interned("name"));

    const bool ordering_equal = !(definition < expansion) && !(expansion < definition);
    assert((definition == expansion) == ordering_equal);

    std::set<Ident> names;
    names.insert(definition);
    names.insert(expansion);
    assert(names.size() == 2);
}
