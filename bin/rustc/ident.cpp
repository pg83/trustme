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
