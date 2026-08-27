#include "ident.h"

#include <std/mem/obj_pool.h>

#include <cassert>
#include <set>

int main() {
    auto pool = stl::ObjPool::fromMemory();
    u32 id = 0;
    auto definitionContext = Ident::Hygiene::newScope(id, *pool);
    auto expansionContext = Ident::Hygiene::newScopeChained(id, *pool, definitionContext);

    Ident definition(definitionContext, RcString::newInterned("name"));
    Ident expansion(expansionContext, RcString::newInterned("name"));

    const bool orderingEqual = !(definition < expansion) && !(expansion < definition);
    assert((definition == expansion) == orderingEqual);

    std::set<Ident> names;
    names.insert(definition);
    names.insert(expansion);
    assert(names.size() == 2);
}
