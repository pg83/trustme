#include "ident.h"

#include "common.h"
#include "output.h"

#include <std/mem/obj_pool.h>

#include <set>

using namespace stl;

namespace {
    constexpr unsigned int ITEM_OPAQUE = 1u << 31;

    unsigned int macroDefinitionId(unsigned int definition) {
        return definition & ~ITEM_OPAQUE;
    }

    unsigned int hygieneDepth(const Ident::Hygiene::Inner* inner) {
        return inner ? inner->depth : 0;
    }

    const Ident::Hygiene::Inner* ancestorAtDepth(const Ident::Hygiene::Inner* inner, unsigned int depth) {
        if (!depth) {
            return nullptr;
        }
        while (inner && inner->depth > depth) {
            inner = inner->parent;
        }
        return inner && inner->depth ? inner : nullptr;
    }

    bool sameHygieneChain(const Ident::Hygiene::Inner* a, const Ident::Hygiene::Inner* b) {
        if (a == b) {
            return true;
        }
        if (!a || !b || a->depth != b->depth || a->context != b->context || a->macroDefinition != b->macroDefinition) {
            return false;
        }
        return sameHygieneChain(a->parent, b->parent);
    }

    Ordering compareContexts(const Ident::Hygiene::Inner* a, const Ident::Hygiene::Inner* b) {
        if (a == b) {
            return OrdEqual;
        }
        if (!a) {
            return b ? OrdLess : OrdEqual;
        }
        if (!b) {
            return OrdGreater;
        }
        auto rv = compareContexts(a->parent, b->parent);
        if (rv != OrdEqual) {
            return rv;
        }
        return a->context < b->context ? OrdLess : a->context > b->context ? OrdGreater : OrdEqual;
    }

    Ordering compareMacroDefinitions(const Ident::Hygiene::Inner* a, const Ident::Hygiene::Inner* b) {
        if (a == b) {
            return OrdEqual;
        }
        if (!a) {
            return b ? OrdLess : OrdEqual;
        }
        if (!b) {
            return OrdGreater;
        }
        auto rv = compareMacroDefinitions(a->parent, b->parent);
        if (rv != OrdEqual) {
            return rv;
        }
        return a->macroDefinition < b->macroDefinition ? OrdLess : a->macroDefinition > b->macroDefinition ? OrdGreater : OrdEqual;
    }

    void appendOpaqueContexts(StringBuilder& os, const Ident::Hygiene::Inner* inner) {
        if (!inner) {
            return;
        }
        appendOpaqueContexts(os, inner->parent);
        if ((inner->macroDefinition & ITEM_OPAQUE) != 0) {
            os << StringView("_") << inner->context;
        }
    }

    void formatHygiene(ZeroCopyOutput& os, const Ident::Hygiene::Inner* inner) {
        if (!inner) {
            return;
        }
        formatHygiene(os, inner->parent);
        if (inner->parent) {
            os << StringView(", ");
        }
        os << inner->context;
        if (inner->macroDefinition != 0) {
            os << StringView("@") << macroDefinitionId(inner->macroDefinition);
            if ((inner->macroDefinition & ITEM_OPAQUE) != 0) {
                os << StringView("#item");
            }
        }
    }
}

bool Ident::Hygiene::isVisible(const Hygiene& srcH) const {
    const auto selfSize = hygieneDepth(inner);
    const auto srcSize = hygieneDepth(srcH.inner);
    if (selfSize > srcSize) {
        return false;
    }
    auto src = srcH.inner;
    auto srcPrefix = ancestorAtDepth(src, selfSize);
    if (!sameHygieneChain(inner && inner->depth ? inner : nullptr, srcPrefix)) {
        return false;
    }
    while (src && src->depth > selfSize) {
        if (src->macroDefinition != 0) {
            return false;
        }
        src = src->parent;
    }
    return true;
}

RcString Ident::Hygiene::applyToItemName(const RcString& name) const {
    if (!inner) {
        return name;
    }
    bool hasOpaqueContext = false;
    for (auto* context = inner; context; context = context->parent) {
        hasOpaqueContext |= (context->macroDefinition & ITEM_OPAQUE) != 0;
    }
    if (!hasOpaqueContext) {
        return name;
    }

    StringBuilder os;
    os << name << StringView("#h");
    appendOpaqueContexts(os, inner);
    return RcString::newInterned(std::string(static_cast<const char*>(os.data()), os.length()));
}

const Ident::Hygiene::Inner* Ident::Hygiene::store(ObjPool& pool, Inner v) {
    return pool.make<Inner>(std::move(v));
}

Ident::Hygiene Ident::Hygiene::newScope(u32& id, ObjPool& pool) {
    Inner v;
    v.context = ++id;
    v.depth = 1;
    return Hygiene(store(pool, std::move(v)));
}

Ident::Hygiene Ident::Hygiene::newScopeChained(u32& id, ObjPool& pool, const Hygiene& parent, unsigned int macroDefinition, bool itemOpaque) {
    Inner v;
    if (parent.inner) {
        v.searchModule = parent.inner->searchModule;
    }
    v.parent = parent.inner && parent.inner->depth ? parent.inner : nullptr;
    v.context = ++id;
    v.depth = hygieneDepth(parent.inner) + 1;
    BUG_ASSERT((macroDefinition & ITEM_OPAQUE) == 0);
    v.macroDefinition = macroDefinition | (itemOpaque ? ITEM_OPAQUE : 0);
    return Hygiene(store(pool, std::move(v)));
}

Ident::Hygiene Ident::Hygiene::withTailScope(ObjPool& pool, const Hygiene& scope, bool inheritModPath) const {
    BUG_ASSERT(scope.inner && scope.inner->depth);
    const auto& s = *scope.inner;
    Inner v;
    v.parent = inner && inner->depth ? inner : nullptr;
    v.context = s.context;
    v.macroDefinition = s.macroDefinition;
    v.depth = hygieneDepth(inner) + 1;
    if (inner) {
        v.searchModule = inner->searchModule;
    }
    if (inheritModPath && s.searchModule) {
        v.searchModule = s.searchModule;
    }
    return Hygiene(store(pool, std::move(v)));
}

Ident::Hygiene Ident::Hygiene::getParent(ObjPool& pool) const {
    BUG_ASSERT(inner && inner->depth);
    if (!inner->parent || !inner->parent->searchModule) {
        return Hygiene(inner->parent);
    }
    Inner v = *inner->parent;
    v.searchModule.reset();
    return Hygiene(store(pool, std::move(v)));
}

bool Ident::Hygiene::leaveMacroDefinition(ObjPool& pool, unsigned int definition, const Hygiene& tokenContext, const Hygiene& definitionContext) {
    if (!inner || !inner->depth) {
        return false;
    }
    const auto& c = *inner;
    if (macroDefinitionId(c.macroDefinition) != definition) {
        return false;
    }
    if (c.parent && c.parent->searchModule == c.searchModule) {
        *this = Hygiene(c.parent);
    } else if (c.parent) {
        Inner v = *c.parent;
        v.searchModule = c.searchModule;
        *this = Hygiene(store(pool, std::move(v)));
    } else if (c.searchModule) {
        Inner v;
        v.searchModule = c.searchModule;
        *this = Hygiene(store(pool, std::move(v)));
    } else {
        *this = Hygiene();
    }
    if (*this == tokenContext) {
        *this = definitionContext;
    }
    return true;
}

void Ident::Hygiene::setModPath(ObjPool& pool, ModPath p) {
    Inner v = inner ? *inner : Inner{};
    v.searchModule = std::make_shared<ModPath>(std::move(p));
    *this = Hygiene(store(pool, std::move(v)));
}

const Ident::ModPath& Ident::Hygiene::modPath() const {
    BUG_ASSERT(inner && inner->searchModule);
    return *inner->searchModule;
}

Ordering Ident::Hygiene::ord(const Hygiene& x) const {
    if (inner == x.inner) {
        return OrdEqual;
    }
    const auto aDepth = hygieneDepth(inner);
    const auto bDepth = hygieneDepth(x.inner);
    const auto* a = ancestorAtDepth(inner, std::min(aDepth, bDepth));
    const auto* b = ancestorAtDepth(x.inner, std::min(aDepth, bDepth));
    auto rv = compareContexts(a, b);
    if (rv != OrdEqual) {
        return rv;
    }
    if (aDepth != bDepth) {
        return aDepth < bDepth ? OrdLess : OrdGreater;
    }
    return compareMacroDefinitions(a, b);
}

Ident::Ident(const char* name)
    : hygiene()
    , name(name)
{
}

Ident::Ident(RcString name)
    : hygiene()
    , name(std::move(name))
{
}

Ident::Ident(Hygiene hygiene, RcString name)
    : hygiene(std::move(hygiene))
    , name(std::move(name))
{
}

bool Ident::operator<(const Ident& x) const {
    if (this->name != x.name) {
        return this->name < x.name;
    }
    if (this->hygiene != x.hygiene) {
        return this->hygiene < x.hygiene;
    }
    return false;
}

void Ident::Hygiene::fmt(ZeroCopyOutput& os) const {
    os << StringView("/*[");
    formatHygiene(os, inner && inner->depth ? inner : nullptr);
    os << StringView("]");
    if (inner && inner->searchModule) {
        os << StringView(" ") << *inner->searchModule;
    }
    os << StringView("*/");
}

template <>
void stl::output<ZeroCopyOutput, Ident>(ZeroCopyOutput& os, Ident x) {
    os << x.name << x.hygiene;
    return;
}

template <>
void stl::output<ZeroCopyOutput, Ident::Hygiene>(ZeroCopyOutput& os, Ident::Hygiene x) {
    x.fmt(os);
}

template <>
void stl::output<ZeroCopyOutput, Ident::ModPath>(ZeroCopyOutput& os, const Ident::ModPath& x) {
    os << StringView("::\"") << x.crate << StringView("\"");
    for (const auto& e : x.ents) {
        os << StringView("::") << e;
    }
    return;
}

template <>
void stl::output<ZeroCopyOutput, std::set<Ident>>(ZeroCopyOutput& out, const std::set<Ident>& values) {
    outCont(out, values);
}
