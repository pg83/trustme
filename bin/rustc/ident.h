#pragma once

#include <vector>
#include <string>
#include <memory>
#include "rc_string.h"

struct Ident {
    // TODO: Use AST::AbsolutePath instead
    struct ModPath {
        RcString crate;
        ::std::vector<RcString> ents;
        friend std::ostream& operator<<(std::ostream& os, const ModPath& x);
    };

    // TODO: make this a reference-counted pointer instead (so it's cheaper to copy)
    // - Presents challenges with setting the module path, and how this is used in macros.
    class Hygiene {
        static unsigned g_next_scope;

        struct Inner {
            ::std::vector<unsigned int> contexts;
            // Zero for lexical scopes, otherwise the macro definition whose
            // invocation introduced the corresponding context.
            ::std::vector<unsigned int> macro_definitions;
            ::std::shared_ptr<ModPath> search_module;
        };

        // NOTE: Use a unique pointer to reduce the size to 1 pointer (instead of 5)
        // - Used quite a bit, and parse sometimes runs out of stack.
        ::std::unique_ptr<Inner> m_inner;

        Hygiene(unsigned int index)
            : m_inner(new Inner())
        {
            m_inner->contexts.push_back(index);
            m_inner->macro_definitions.push_back(0);
        }

        Inner* operator->() {
            return &*m_inner;
        }

        const Inner* operator->() const {
            return &*m_inner;
        }

    public:
        Hygiene()
            : m_inner(new Inner())
        {
        }

        Hygiene(const Hygiene& x)
            : m_inner(new Inner(*x.m_inner))
        {
        }

        Hygiene& operator=(const Hygiene& x) {
            *this = Hygiene(x);
            assert(this->m_inner);
            return *this;
        }

        //Hygiene(Hygiene&& x) = default;
        Hygiene(Hygiene&& x)
            : m_inner(std::move(x.m_inner))
        {
            //assert(m_inner);
        }

        Hygiene& operator=(Hygiene&& x) {
            m_inner.reset(x.m_inner.release());
            //assert(m_inner);
            return *this;
        }

        static Hygiene new_scope() {
            return Hygiene(++g_next_scope);
        }

        static Hygiene new_scope_chained(const Hygiene& parent, unsigned int macro_definition = 0) {
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

        Hygiene with_tail_scope(const Hygiene& scope, bool inherit_mod_path = false) const {
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

        Hygiene get_parent() const {
            //assert(this->contexts.size() > 1);
            Hygiene rv;
            rv->contexts.insert(rv->contexts.begin(), m_inner->contexts.begin(), m_inner->contexts.end() - 1);
            rv->macro_definitions.insert(rv->macro_definitions.begin(), m_inner->macro_definitions.begin(), m_inner->macro_definitions.end() - 1);
            return rv;
        }

        bool leave_macro_definition(unsigned int definition, const Hygiene& token_context, const Hygiene& definition_context) {
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

        bool has_mod_path() const {
            return m_inner->search_module != 0;
        }

        const ModPath& mod_path() const {
            assert(m_inner->search_module);
            return *m_inner->search_module;
        }

        void set_mod_path(ModPath p) {
            m_inner->search_module.reset(new ModPath(::std::move(p)));
        }

        // Returns true if an ident with hygine `source` can see an ident with this hygine
        bool is_visible(const Hygiene& source) const;

        Ordering ord(const Hygiene& x) const {
            ORD(m_inner->contexts, x->contexts);
            ORD(m_inner->macro_definitions, x->macro_definitions); /*ORD(*m_inner->search_module, *x->search_module);*/
            return OrdEqual;
        }

        bool operator==(const Hygiene& x) const {
            return ord(x) == OrdEqual;
        }

        bool operator!=(const Hygiene& x) const {
            return ord(x) != OrdEqual;
        }

        bool operator<(const Hygiene& x) const {
            return ord(x) == OrdLess;
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const Hygiene& v);
    };

    Hygiene hygiene;
    RcString name;

    Ident(const char* name)
        : hygiene()
        , name(name)
    {
    }

    Ident(RcString name)
        : hygiene()
        , name(::std::move(name))
    {
    }

    Ident(Hygiene hygiene, RcString name)
        : hygiene(::std::move(hygiene))
        , name(::std::move(name))
    {
    }

    Ident(Ident&& x) = default;
    Ident(const Ident& x) = default;
    Ident& operator=(Ident&& x) = default;
    Ident& operator=(const Ident& x) = default;

    RcString into_string() {
        return ::std::move(name);
    }

    bool operator==(const char* s) const {
        return this->name == s;
    }

    bool same_name(const Ident& x) const {
        return this->name == x.name;
    }

    bool operator==(const Ident& x) const {
        return this->name == x.name && this->hygiene == x.hygiene;
    }

    bool operator!=(const Ident& x) const {
        return !(*this == x);
    }

    bool operator<(const Ident& x) const {
        if (this->name != x.name) {
            return this->name < x.name;
        }
        if (this->hygiene != x.hygiene) {
            return this->hygiene < x.hygiene;
        }
        return false;
    }

    friend ::std::ostream& operator<<(::std::ostream& os, const Ident& x);
};
