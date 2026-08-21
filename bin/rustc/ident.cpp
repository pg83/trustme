#include "ident.h"
#include <iostream>
#include "debug.h"
#include "common.h" // vector print

#include <std/mem/obj_pool.h>

unsigned int Ident::Hygiene::gNextScope = 0;

namespace {
    constexpr unsigned int ITEM_OPAQUE = 1u << 31;

    unsigned int macroDefinitionId(unsigned int definition) {
        return definition & ~ITEM_OPAQUE;
    }
}

bool Ident::Hygiene::isVisible(const Hygiene& srcH) const {
    const auto& self = get();
    const auto& src = srcH.get();
    if (self.contexts.size() > src.contexts.size()) {
        return false;
    }
    for (size_t i = 0; i < self.contexts.size(); i++) {
        if (self.contexts[i] != src.contexts[i] || self.macroDefinitions[i] != src.macroDefinitions[i]) {
            return false;
        }
    }
    // Ordinary lexical scopes extend visibility. A macro invocation is a
    // semi-opaque rib: it must be removed at its definition boundary before
    // bindings outside that definition become visible.
    for (size_t i = self.contexts.size(); i < src.contexts.size(); i++) {
        if (src.macroDefinitions[i] != 0) {
            return false;
        }
    }
    return true;
}

RcString Ident::Hygiene::applyToItemName(const RcString& name) const {
    const auto& h = get();
    bool hasOpaqueContext = false;
    for (const auto definition : h.macroDefinitions) {
        hasOpaqueContext |= (definition & ITEM_OPAQUE) != 0;
    }
    if (!hasOpaqueContext) {
        return name;
    }

    ::std::ostringstream os;
    os << name << "#h";
    for (size_t i = 0; i < h.contexts.size(); i++) {
        if ((h.macroDefinitions[i] & ITEM_OPAQUE) != 0) {
            os << "_" << h.contexts[i];
        }
    }
    return RcString::newInterned(os.str());
}

::std::ostream& operator<<(::std::ostream& os, const Ident& x) {
    os << x.name << x.hygiene;
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const Ident::Hygiene& x) {
    const auto& h = x.get();
    os << "/*[";
    for (size_t i = 0; i < h.contexts.size(); i++) {
        if (i != 0) {
            os << ", ";
        }
        os << h.contexts[i];
        if (h.macroDefinitions[i] != 0) {
            os << "@" << macroDefinitionId(h.macroDefinitions[i]);
            if ((h.macroDefinitions[i] & ITEM_OPAQUE) != 0) {
                os << "#item";
            }
        }
    }
    os << "]";
    if (h.searchModule) {
        os << " " << *h.searchModule;
    }
    os << "*/";
    return os;
}

std::ostream& operator<<(std::ostream& os, const Ident::ModPath& x) {
    os << "::\"" << x.crate << "\"";
    for (const auto& e : x.ents) {
        os << "::" << e;
    }
    return os;
}

const Ident::Hygiene::Inner& Ident::Hygiene::get() const {
    static const Inner sEmpty;
    return inner ? *inner : sEmpty;
}

Ident::Hygiene::Inner Ident::Hygiene::clone() const {
    return get();
}

const Ident::Hygiene::Inner* Ident::Hygiene::store(stl::ObjPool& pool, Inner v) {
    return pool.make<Inner>(::std::move(v));
}

Ident::Hygiene Ident::Hygiene::newScope(stl::ObjPool& pool) {
    Inner v;
    v.contexts.push_back(++gNextScope);
    v.macroDefinitions.push_back(0);
    return Hygiene(store(pool, ::std::move(v)));
}

Ident::Hygiene Ident::Hygiene::newScopeChained(stl::ObjPool& pool, const Hygiene& parent, unsigned int macroDefinition, bool itemOpaque) {
    const auto& p = parent.get();
    Inner v;
    v.searchModule = p.searchModule;
    v.contexts.reserve(p.contexts.size() + 1);
    v.macroDefinitions.reserve(p.macroDefinitions.size() + 1);
    v.contexts = p.contexts;
    v.macroDefinitions = p.macroDefinitions;
    v.contexts.push_back(++gNextScope);
    assert((macroDefinition & ITEM_OPAQUE) == 0);
    v.macroDefinitions.push_back(macroDefinition | (itemOpaque ? ITEM_OPAQUE : 0));
    return Hygiene(store(pool, ::std::move(v)));
}

Ident::Hygiene Ident::Hygiene::withTailScope(stl::ObjPool& pool, const Hygiene& scope, bool inheritModPath) const {
    const auto& s = scope.get();
    assert(!s.contexts.empty());
    assert(s.contexts.size() == s.macroDefinitions.size());
    Inner v = clone();
    v.contexts.push_back(s.contexts.back());
    v.macroDefinitions.push_back(s.macroDefinitions.back());
    if (inheritModPath && s.searchModule) {
        v.searchModule = s.searchModule;
    }
    return Hygiene(store(pool, ::std::move(v)));
}

Ident::Hygiene Ident::Hygiene::getParent(stl::ObjPool& pool) const {
    const auto& c = get();
    Inner v;
    v.contexts.insert(v.contexts.begin(), c.contexts.begin(), c.contexts.end() - 1);
    v.macroDefinitions.insert(v.macroDefinitions.begin(), c.macroDefinitions.begin(), c.macroDefinitions.end() - 1);
    return Hygiene(store(pool, ::std::move(v)));
}

bool Ident::Hygiene::leaveMacroDefinition(stl::ObjPool& pool, unsigned int definition, const Hygiene& tokenContext, const Hygiene& definitionContext) {
    const auto& c = get();
    assert(c.contexts.size() == c.macroDefinitions.size());
    if (c.macroDefinitions.empty() || macroDefinitionId(c.macroDefinitions.back()) != definition) {
        return false;
    }
    Inner v = clone();
    v.contexts.pop_back();
    v.macroDefinitions.pop_back();
    *this = Hygiene(store(pool, ::std::move(v)));
    if (*this == tokenContext) {
        *this = definitionContext;
    }
    return true;
}

void Ident::Hygiene::setModPath(stl::ObjPool& pool, ModPath p) {
    Inner v = clone();
    v.searchModule = ::std::make_shared<ModPath>(::std::move(p));
    *this = Hygiene(store(pool, ::std::move(v)));
}

const Ident::ModPath& Ident::Hygiene::modPath() const {
    assert(inner && inner->searchModule);
    return *inner->searchModule;
}

Ordering Ident::Hygiene::ord(const Hygiene& x) const {
    if (inner == x.inner) {
        return OrdEqual;
    }
    const auto& a = get();
    const auto& b = x.get();
    ORD(a.contexts, b.contexts);
    ORD(a.macroDefinitions, b.macroDefinitions); /*ORD(*m_inner->search_module, *x->search_module);*/
    return OrdEqual;
}
Ident::Ident(const char* name)
    : hygiene()
    , name(name) {
}
Ident::Ident(RcString name)
    : hygiene()
    , name(::std::move(name)) {
}
Ident::Ident(Hygiene hygiene, RcString name)
    : hygiene(::std::move(hygiene))
    , name(::std::move(name)) {
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
