#include "ident.h"
#include <iostream>
#include "debug.h"
#include "common.h" // vector print

unsigned int Ident::Hygiene::g_next_scope = 0;

bool Ident::Hygiene::is_visible(const Hygiene& src) const {
    if (m_inner->contexts.size() > src->contexts.size()) {
        return false;
    }
    for (size_t i = 0; i < m_inner->contexts.size(); i++) {
        if (m_inner->contexts[i] != src->contexts[i] || m_inner->macro_definitions[i] != src->macro_definitions[i]) {
            return false;
        }
    }
    // Ordinary lexical scopes extend visibility. A macro invocation is a
    // semi-opaque rib: it must be removed at its definition boundary before
    // bindings outside that definition become visible.
    for (size_t i = m_inner->contexts.size(); i < src->contexts.size(); i++) {
        if (src->macro_definitions[i] != 0) {
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
        if (x->macro_definitions[i] != 0) {
            os << "@" << x->macro_definitions[i];
        }
    }
    os << "]";
    if (x->search_module) {
        os << " " << *x->search_module;
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
    : m_inner(new Inner()) {
    m_inner->contexts.push_back(index);
    m_inner->macro_definitions.push_back(0);
}
Ident::Hygiene::Hygiene()
    : m_inner(new Inner()) {
}
Ident::Hygiene::Hygiene(const Hygiene& x)
    : m_inner(new Inner(*x.m_inner)) {
}
Ident::Hygiene& Ident::Hygiene::operator=(const Hygiene& x) {
    *this = Hygiene(x);
    assert(this->m_inner);
    return *this;
}
//Hygiene(Hygiene&& x) = default;
Ident::Hygiene::Hygiene(Hygiene&& x)
    : m_inner(std::move(x.m_inner)) {
    //assert(m_inner);
}
Ident::Hygiene& Ident::Hygiene::operator=(Hygiene&& x) {
    m_inner.reset(x.m_inner.release());
    //assert(m_inner);
    return *this;
}
Ident::Hygiene Ident::Hygiene::new_scope_chained(const Hygiene& parent, unsigned int macro_definition) {
    Hygiene rv;
    rv->search_module = parent->search_module;
    rv->contexts.reserve(parent->contexts.size() + 1);
    rv->macro_definitions.reserve(parent->macro_definitions.size() + 1);
    rv->contexts.insert(rv->contexts.begin(), parent->contexts.begin(), parent->contexts.end());
    rv->macro_definitions.insert(rv->macro_definitions.begin(), parent->macro_definitions.begin(), parent->macro_definitions.end());
    rv->contexts.push_back(++g_next_scope);
    rv->macro_definitions.push_back(macro_definition);
    return rv;
}
Ident::Hygiene Ident::Hygiene::with_tail_scope(const Hygiene& scope, bool inherit_mod_path) const {
    assert(!scope->contexts.empty());
    assert(scope->contexts.size() == scope->macro_definitions.size());
    Hygiene rv(*this);
    rv->contexts.push_back(scope->contexts.back());
    rv->macro_definitions.push_back(scope->macro_definitions.back());
    if (inherit_mod_path && scope->search_module) {
        rv->search_module = scope->search_module;
    }
    return rv;
}
Ident::Hygiene Ident::Hygiene::get_parent() const {
    //assert(this->contexts.size() > 1);
    Hygiene rv;
    rv->contexts.insert(rv->contexts.begin(), m_inner->contexts.begin(), m_inner->contexts.end() - 1);
    rv->macro_definitions.insert(rv->macro_definitions.begin(), m_inner->macro_definitions.begin(), m_inner->macro_definitions.end() - 1);
    return rv;
}
bool Ident::Hygiene::leave_macro_definition(unsigned int definition, const Hygiene& token_context, const Hygiene& definition_context) {
    assert(m_inner->contexts.size() == m_inner->macro_definitions.size());
    if (m_inner->macro_definitions.empty() || m_inner->macro_definitions.back() != definition) {
        return false;
    }
    m_inner->contexts.pop_back();
    m_inner->macro_definitions.pop_back();
    if (*this == token_context) {
        *this = definition_context;
    }
    return true;
}
const Ident::ModPath& Ident::Hygiene::mod_path() const {
    assert(m_inner->search_module);
    return *m_inner->search_module;
}
Ordering Ident::Hygiene::ord(const Hygiene& x) const {
    ORD(m_inner->contexts, x->contexts);
    ORD(m_inner->macro_definitions, x->macro_definitions); /*ORD(*m_inner->search_module, *x->search_module);*/
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
