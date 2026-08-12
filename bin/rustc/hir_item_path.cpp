#include "hir_item_path.h"

namespace HIR {

ItemPath::ItemPath(const char* crate)
    : crateName(crate) {
}
ItemPath::ItemPath(const ::std::string& crate)
    : crateName(crate.c_str()) {
}
ItemPath::ItemPath(const RcString& crate)
    : crateName(crate.c_str()) {
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
    , traitParams(&params) {
}
ItemPath::ItemPath(const ::HIR::SimplePath& path)
    : trait(&path) {
}
::HIR::SimplePath ItemPath::getSimplePath() const {
    if (wrapped) {
        assert(wrapped->mData.is_Generic());
        return wrapped->mData.as_Generic().mPath;
    } else if (trait && !name) {
        return trait->clone();
    } else if (parent) {
        assert(name);
        return parent->getSimplePath() + RcString::newInterned(name);
    } else {
        assert(!name);
        assert(crateName);
        return ::HIR::SimplePath(RcString::newInterned(crateName));
    }
}
::HIR::Path ItemPath::getFullPath() const {
    if (wrapped) {
        return wrapped->clone();
    }
    assert(parent);
    assert(name);

    // If the parent has a name, or the parent is the crate root.
    if (parent->name || !parent->ty) {
        return getSimplePath();
    } else if (parent->trait) {
        assert(parent->ty);
        assert(parent->traitParams);
        return ::HIR::Path(parent->ty, ::HIR::GenericPath(parent->trait->clone(), parent->traitParams->clone()), RcString::newInterned(name));
    } else {
        assert(parent->ty);
        return ::HIR::Path(parent->ty, RcString::newInterned(name));
    }
}
const ItemPath& ItemPath::getTopIp() const {
    if (this->parent) {
        return this->parent->getTopIp();
    }
    return *this;
}
bool ItemPath::operator==(const ::HIR::SimplePath& sp) const {
    if (sp.crateName() != "") {
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

namespace HIR {

::std::ostream& operator<<(::std::ostream& os, const ItemPath& x) {
    if (x.wrapped) {
        return os << *x.wrapped;
    }
    if (x.parent) {
        os << *x.parent;
    }
    if (x.name) {
        os << "::" << x.name;
    } else if (x.ty) {
        os << "<" << *x.ty;
        if (x.trait) {
            os << " as " << *x.trait;
            if (x.traitParams) {
                os << *x.traitParams;
            }
        }
        os << ">";
    } else if (x.trait) {
        os << "<* as " << *x.trait << ">";
    } else if (x.crateName) {
        os << "::\"" << x.crateName << "\"";
    }
    return os;
}
}
