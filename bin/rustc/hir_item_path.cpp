#include "hir_item_path.h"

HIRItemPath::HIRItemPath(const char* crate)
    : crateName(crate)
{
}

HIRItemPath::HIRItemPath(const ::std::string& crate)
    : crateName(crate.c_str())
{
}

HIRItemPath::HIRItemPath(const RcString& crate)
    : crateName(crate.c_str())
{
}

HIRItemPath::HIRItemPath(const HIRItemPath& p, const char* n)
    : parent(&p)
    , name(n)
{
}

HIRItemPath::HIRItemPath(const HIRPath& p)
    : wrapped(&p)
{
}

HIRItemPath::HIRItemPath(const HIRTypeData* type)
    : ty(type)
{
}

HIRItemPath::HIRItemPath(const HIRTypeData* type, const HIRSimplePath& path, const HIRPathParams& params)
    : ty(type)
    , trait(&path)
    , traitParams(&params)
{
}

HIRItemPath::HIRItemPath(const HIRSimplePath& path)
    : trait(&path)
{
}

HIRSimplePath HIRItemPath::getSimplePath() const {
    if (wrapped) {
        assert(wrapped->data.is_Generic());
        return wrapped->data.as_Generic().path;
    } else if (trait && !name) {
        return trait->clone();
    } else if (parent) {
        assert(name);
        return parent->getSimplePath() + RcString::newInterned(name);
    } else {
        assert(!name);
        assert(crateName);
        return HIRSimplePath(RcString::newInterned(crateName));
    }
}

HIRPath HIRItemPath::getFullPath() const {
    if (wrapped) {
        return wrapped->clone();
    }
    assert(parent);
    assert(name);

    if (parent->name || !parent->ty) {
        return getSimplePath();
    } else if (parent->trait) {
        assert(parent->ty);
        assert(parent->traitParams);
        return HIRPath(parent->ty, HIRGenericPath(parent->trait->clone(), parent->traitParams->clone()), RcString::newInterned(name));
    } else {
        assert(parent->ty);
        return HIRPath(parent->ty, RcString::newInterned(name));
    }
}

const HIRItemPath& HIRItemPath::getTopIp() const {
    if (this->parent) {
        return this->parent->getTopIp();
    }
    return *this;
}

bool HIRItemPath::operator==(const HIRSimplePath& sp) const {
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

::std::ostream& operator<<(::std::ostream& os, const HIRItemPath& x) {
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
