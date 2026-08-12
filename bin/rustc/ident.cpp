#include "ident.h"
#include <iostream>
#include "debug.h"
#include "common.h" // vector print

unsigned int Ident::Hygiene::gNextScope = 0;

bool Ident::Hygiene::isVisible(const Hygiene& src) const {
    if (inner->contexts.size() > src->contexts.size()) {
        return false;
    }
    for (size_t i = 0; i < inner->contexts.size(); i++) {
        if (inner->contexts[i] != src->contexts[i] || inner->macroDefinitions[i] != src->macroDefinitions[i]) {
            return false;
        }
    }
    // Ordinary lexical scopes extend visibility. A macro invocation is a
    // semi-opaque rib: it must be removed at its definition boundary before
    // bindings outside that definition become visible.
    for (size_t i = inner->contexts.size(); i < src->contexts.size(); i++) {
        if (src->macroDefinitions[i] != 0) {
            return false;
        }
    }
    return true;
}

::std::ostream& operator<<(::std::ostream& os, const Ident& x) {
    os << x.name << x.hygiene;
    return os;
}

::std::ostream& operator<<(::std::ostream& os, const Ident::Hygiene& x) {
    os << "/*[";
    for (size_t i = 0; i < x->contexts.size(); i++) {
        if (i != 0) {
            os << ", ";
        }
        os << x->contexts[i];
        if (x->macroDefinitions[i] != 0) {
            os << "@" << x->macroDefinitions[i];
        }
    }
    os << "]";
    if (x->searchModule) {
        os << " " << *x->searchModule;
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

Ident::Hygiene::Hygiene(unsigned int index)
    : inner(new Inner()) {
    inner->contexts.push_back(index);
    inner->macroDefinitions.push_back(0);
}
Ident::Hygiene::Hygiene()
    : inner(new Inner()) {
}
Ident::Hygiene::Hygiene(const Hygiene& x)
    : inner(new Inner(*x.inner)) {
}
Ident::Hygiene& Ident::Hygiene::operator=(const Hygiene& x) {
    *this = Hygiene(x);
    assert(this->inner);
    return *this;
}
Ident::Hygiene::Hygiene(Hygiene&& x)
    : inner(std::move(x.inner)) {
}
Ident::Hygiene& Ident::Hygiene::operator=(Hygiene&& x) {
    inner.reset(x.inner.release());
    return *this;
}
Ident::Hygiene Ident::Hygiene::newScopeChained(const Hygiene& parent, unsigned int macroDefinition) {
    Hygiene rv;
    rv->searchModule = parent->searchModule;
    rv->contexts.reserve(parent->contexts.size() + 1);
    rv->macroDefinitions.reserve(parent->macroDefinitions.size() + 1);
    rv->contexts.insert(rv->contexts.begin(), parent->contexts.begin(), parent->contexts.end());
    rv->macroDefinitions.insert(rv->macroDefinitions.begin(), parent->macroDefinitions.begin(), parent->macroDefinitions.end());
    rv->contexts.push_back(++gNextScope);
    rv->macroDefinitions.push_back(macroDefinition);
    return rv;
}
Ident::Hygiene Ident::Hygiene::withTailScope(const Hygiene& scope, bool inheritModPath) const {
    assert(!scope->contexts.empty());
    assert(scope->contexts.size() == scope->macroDefinitions.size());
    Hygiene rv(*this);
    rv->contexts.push_back(scope->contexts.back());
    rv->macroDefinitions.push_back(scope->macroDefinitions.back());
    if (inheritModPath && scope->searchModule) {
        rv->searchModule = scope->searchModule;
    }
    return rv;
}
Ident::Hygiene Ident::Hygiene::getParent() const {
    Hygiene rv;
    rv->contexts.insert(rv->contexts.begin(), inner->contexts.begin(), inner->contexts.end() - 1);
    rv->macroDefinitions.insert(rv->macroDefinitions.begin(), inner->macroDefinitions.begin(), inner->macroDefinitions.end() - 1);
    return rv;
}
bool Ident::Hygiene::leaveMacroDefinition(unsigned int definition, const Hygiene& tokenContext, const Hygiene& definitionContext) {
    assert(inner->contexts.size() == inner->macroDefinitions.size());
    if (inner->macroDefinitions.empty() || inner->macroDefinitions.back() != definition) {
        return false;
    }
    inner->contexts.pop_back();
    inner->macroDefinitions.pop_back();
    if (*this == tokenContext) {
        *this = definitionContext;
    }
    return true;
}
const Ident::ModPath& Ident::Hygiene::modPath() const {
    assert(inner->searchModule);
    return *inner->searchModule;
}
Ordering Ident::Hygiene::ord(const Hygiene& x) const {
    ORD(inner->contexts, x->contexts);
    ORD(inner->macroDefinitions, x->macroDefinitions); /*ORD(*m_inner->search_module, *x->search_module);*/
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
