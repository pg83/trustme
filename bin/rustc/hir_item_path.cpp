#include "hir_item_path.h"

namespace HIR {

ItemPath::ItemPath(const char* crate)
    : crate_name(crate) {
}
ItemPath::ItemPath(const ::std::string& crate)
    : crate_name(crate.c_str()) {
}
ItemPath::ItemPath(const RcString& crate)
    : crate_name(crate.c_str()) {
}
ItemPath::ItemPath(const ItemPath& p, const char* n)
    : parent(&p)
    , name(n) {
}
ItemPath::ItemPath(const ::HIR::Path& p)
    : wrapped(&p) {
}
ItemPath::ItemPath(const ::HIR::TypeData* type)
    : ty(type) {
}
ItemPath::ItemPath(const ::HIR::TypeData* type, const ::HIR::SimplePath& path, const ::HIR::PathParams& params)
    : ty(type)
    , trait(&path)
    , trait_params(&params) {
}
ItemPath::ItemPath(const ::HIR::SimplePath& path)
    : trait(&path) {
}
::HIR::SimplePath ItemPath::get_simple_path() const {
    if (wrapped) {
        assert(wrapped->m_data.is_Generic());
        return wrapped->m_data.as_Generic().m_path;
    } else if (trait && !name) {
        return trait->clone();
    } else if (parent) {
        assert(name);
        return parent->get_simple_path() + RcString::new_interned(name);
    } else {
        assert(!name);
        assert(crate_name);
        return ::HIR::SimplePath(RcString::new_interned(crate_name));
    }
}
::HIR::Path ItemPath::get_full_path() const {
    if (wrapped) {
        return wrapped->clone();
    }
    assert(parent);
    assert(name);

    // If the parent has a name, or the parent is the crate root.
    if (parent->name || !parent->ty) {
        return get_simple_path();
    } else if (parent->trait) {
        assert(parent->ty);
        assert(parent->trait_params);
        return ::HIR::Path(parent->ty, ::HIR::GenericPath(parent->trait->clone(), parent->trait_params->clone()), RcString::new_interned(name));
    } else {
        assert(parent->ty);
        return ::HIR::Path(parent->ty, RcString::new_interned(name));
    }
}
const ItemPath& ItemPath::get_top_ip() const {
    if (this->parent) {
        return this->parent->get_top_ip();
    }
    return *this;
}
bool ItemPath::operator==(const ::HIR::SimplePath& sp) const {
    if (sp.crate_name() != "") {
        return false;
    }

    auto i = sp.components().size();
    const auto* n = this;
    while (n && i--) {
        if (!n->name) {
            return false;
        }
        if (n->name != sp.components()[i]) {
            return false;
        }
        n = n->parent;
    }
    if (i > 0 || n->name) {
        return false;
    }
    return true;
}
}
