#include "ident.h"
#include "output.h"

#include "common.h"

#include <std/mem/obj_pool.h>


using namespace stl;

namespace {
    constexpr unsigned int ITEM_OPAQUE = 1u << 31;

    unsigned int macroDefinitionId(unsigned int definition) {
        return definition & ~ITEM_OPAQUE;
    }
}

namespace stl {
}

bool Ident::Hygiene::isVisible(const Hygiene& srcH) const {
    const auto selfSize = inner ? inner->contexts.size() : 0;
    const auto srcSize = srcH.inner ? srcH.inner->contexts.size() : 0;
    if (selfSize > srcSize) {
        return false;
    }
    for (size_t i = 0; i < selfSize; i++) {
        if (inner->contexts[i] != srcH.inner->contexts[i] || inner->macroDefinitions[i] != srcH.inner->macroDefinitions[i]) {
            return false;
        }
    }
    for (size_t i = selfSize; i < srcSize; i++) {
        if (srcH.inner->macroDefinitions[i] != 0) {
            return false;
        }
    }
    return true;
}

RcString Ident::Hygiene::applyToItemName(const RcString& name) const {
    if (!inner) {
        return name;
    }
    const auto& h = *inner;
    bool hasOpaqueContext = false;
    for (const auto definition : h.macroDefinitions) {
        hasOpaqueContext |= (definition & ITEM_OPAQUE) != 0;
    }
    if (!hasOpaqueContext) {
        return name;
    }

    StringBuilder os;
    os << name << StringView("#h");
    for (size_t i = 0; i < h.contexts.size(); i++) {
        if ((h.macroDefinitions[i] & ITEM_OPAQUE) != 0) {
            os << StringView("_") << h.contexts[i];
        }
    }
    return RcString::newInterned(std::string(static_cast<const char*>(os.data()), os.length()));
}







Ident::Hygiene::Inner Ident::Hygiene::clone() const {
    return inner ? *inner : Inner{};
}

const Ident::Hygiene::Inner* Ident::Hygiene::store(ObjPool& pool, Inner v) {
    return pool.make<Inner>(std::move(v));
}

Ident::Hygiene Ident::Hygiene::newScope(u32& id, ObjPool& pool) {
    Inner v;
    v.contexts.push_back(++id);
    v.macroDefinitions.push_back(0);
    return Hygiene(store(pool, std::move(v)));
}

Ident::Hygiene Ident::Hygiene::newScopeChained(u32& id, ObjPool& pool, const Hygiene& parent, unsigned int macroDefinition, bool itemOpaque) {
    Inner v;
    if (parent.inner) {
        v.searchModule = parent.inner->searchModule;
        v.contexts = parent.inner->contexts;
        v.macroDefinitions = parent.inner->macroDefinitions;
    }
    v.contexts.reserve(v.contexts.size() + 1);
    v.macroDefinitions.reserve(v.macroDefinitions.size() + 1);
    v.contexts.push_back(++id);
    BUG_ASSERT((macroDefinition & ITEM_OPAQUE) == 0);
    v.macroDefinitions.push_back(macroDefinition | (itemOpaque ? ITEM_OPAQUE : 0));
    return Hygiene(store(pool, std::move(v)));
}

Ident::Hygiene Ident::Hygiene::withTailScope(ObjPool& pool, const Hygiene& scope, bool inheritModPath) const {
    BUG_ASSERT(scope.inner);
    const auto& s = *scope.inner;
    BUG_ASSERT(!s.contexts.empty());
    BUG_ASSERT(s.contexts.size() == s.macroDefinitions.size());
    Inner v = clone();
    v.contexts.push_back(s.contexts.back());
    v.macroDefinitions.push_back(s.macroDefinitions.back());
    if (inheritModPath && s.searchModule) {
        v.searchModule = s.searchModule;
    }
    return Hygiene(store(pool, std::move(v)));
}

Ident::Hygiene Ident::Hygiene::getParent(ObjPool& pool) const {
    BUG_ASSERT(inner);
    const auto& c = *inner;
    Inner v;
    v.contexts.insert(v.contexts.begin(), c.contexts.begin(), c.contexts.end() - 1);
    v.macroDefinitions.insert(v.macroDefinitions.begin(), c.macroDefinitions.begin(), c.macroDefinitions.end() - 1);
    return Hygiene(store(pool, std::move(v)));
}

bool Ident::Hygiene::leaveMacroDefinition(ObjPool& pool, unsigned int definition, const Hygiene& tokenContext, const Hygiene& definitionContext) {
    if (!inner) {
        return false;
    }
    const auto& c = *inner;
    BUG_ASSERT(c.contexts.size() == c.macroDefinitions.size());
    if (c.macroDefinitions.empty() || macroDefinitionId(c.macroDefinitions.back()) != definition) {
        return false;
    }
    Inner v = clone();
    v.contexts.pop_back();
    v.macroDefinitions.pop_back();
    *this = Hygiene(store(pool, std::move(v)));
    if (*this == tokenContext) {
        *this = definitionContext;
    }
    return true;
}

void Ident::Hygiene::setModPath(ObjPool& pool, ModPath p) {
    Inner v = clone();
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
    if (!inner) {
        return x.inner->contexts.empty() && x.inner->macroDefinitions.empty() ? OrdEqual : OrdLess;
    }
    if (!x.inner) {
        return inner->contexts.empty() && inner->macroDefinitions.empty() ? OrdEqual : OrdGreater;
    }
    const auto& a = *inner;
    const auto& b = *x.inner;
    ORD(a.contexts, b.contexts);
    ORD(a.macroDefinitions, b.macroDefinitions); /*ORD(*m_inner->search_module, *x->search_module);*/
    return OrdEqual;
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
    if (inner) {
        for (size_t i = 0; i < inner->contexts.size(); i++) {
            if (i != 0) {
                os << StringView(", ");
            }
            os << inner->contexts[i];
            if (inner->macroDefinitions[i] != 0) {
                os << StringView("@") << macroDefinitionId(inner->macroDefinitions[i]);
                if ((inner->macroDefinitions[i] & ITEM_OPAQUE) != 0) {
                    os << StringView("#item");
                }
            }
        }
    }
    os << StringView("]");
    if (inner && inner->searchModule) {
        os << StringView(" ") << *inner->searchModule;
    }
    os << StringView("*/");
}

namespace stl {
template <>
void output<ZeroCopyOutput, Ident>(ZeroCopyOutput& os, Ident x) {
    os << x.name << x.hygiene;
    return;
}

template <>
void output<ZeroCopyOutput, Ident::Hygiene>(ZeroCopyOutput& os, Ident::Hygiene x) {
    x.fmt(os);
}

template <>
void output<ZeroCopyOutput, Ident::ModPath>(ZeroCopyOutput& os, const Ident::ModPath& x) {
    os << StringView("::\"") << x.crate << StringView("\"");
    for (const auto& e : x.ents) {
        os << StringView("::") << e;
    }
    return;
}

template <>
void output<ZeroCopyOutput, std::set<Ident>>(ZeroCopyOutput& out, const std::set<Ident>& values) {
    outCont(out, values);
}
}
