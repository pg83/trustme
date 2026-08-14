#pragma once

#include <vector>
#include <string>
#include <memory>
#include "rc_string.h"

namespace stl {
    class ObjPool;
}

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
        static unsigned gNextScope;

    public:
        // Immutable once built: every mutating operation clones the node into
        // the pool and re-points, so a Hygiene value is just a pointer and
        // copies are free. A null pointer is the empty hygiene, which is by
        // far the most common case and costs no allocation at all.
        struct Inner {
            ::std::vector<unsigned int> contexts;
            // Zero for lexical scopes, otherwise the macro definition whose
            // invocation introduced the corresponding context.
            ::std::vector<unsigned int> macroDefinitions;
            ::std::shared_ptr<ModPath> searchModule;
        };

    private:
        const Inner* inner = nullptr;

        explicit Hygiene(const Inner* inner)
            : inner(inner)
        {
        }

        // Read-only view that works for the empty (null) hygiene too.
        const Inner& get() const;
        // Copy of the current contents, for a mutation to modify and re-intern.
        Inner clone() const;
        static const Inner* store(stl::ObjPool& pool, Inner v);

    public:
        Hygiene() = default;
        Hygiene(const Hygiene& x) = default;
        Hygiene& operator=(const Hygiene& x) = default;
        Hygiene(Hygiene&& x) = default;
        Hygiene& operator=(Hygiene&& x) = default;

        static Hygiene newScope(stl::ObjPool& pool);

        static Hygiene newScopeChained(stl::ObjPool& pool, const Hygiene& parent, unsigned int macroDefinition = 0);

        Hygiene withTailScope(stl::ObjPool& pool, const Hygiene& scope, bool inheritModPath = false) const;

        Hygiene getParent(stl::ObjPool& pool) const;

        bool leaveMacroDefinition(stl::ObjPool& pool, unsigned int definition, const Hygiene& tokenContext, const Hygiene& definitionContext);

        bool hasModPath() const {
            return inner && inner->searchModule;
        }

        const ModPath& modPath() const;

        void setModPath(stl::ObjPool& pool, ModPath p);

        // Returns true if an ident with hygine `source` can see an ident with this hygine
        bool isVisible(const Hygiene& source) const;

        Ordering ord(const Hygiene& x) const;

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

    Ident(const char* name);

    Ident(RcString name);

    Ident(Hygiene hygiene, RcString name);

    Ident(Ident&& x) = default;
    Ident(const Ident& x) = default;
    Ident& operator=(Ident&& x) = default;
    Ident& operator=(const Ident& x) = default;

    RcString intoString() {
        return ::std::move(name);
    }

    bool operator==(const char* s) const {
        return this->name == s;
    }

    bool sameName(const Ident& x) const {
        return this->name == x.name;
    }

    bool operator==(const Ident& x) const {
        return this->name == x.name && this->hygiene == x.hygiene;
    }

    bool operator!=(const Ident& x) const {
        return !(*this == x);
    }

    bool operator<(const Ident& x) const;

    friend ::std::ostream& operator<<(::std::ostream& os, const Ident& x);
};
