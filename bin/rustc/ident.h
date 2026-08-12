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
        static unsigned gNextScope;

        struct Inner {
            ::std::vector<unsigned int> contexts;
            // Zero for lexical scopes, otherwise the macro definition whose
            // invocation introduced the corresponding context.
            ::std::vector<unsigned int> macroDefinitions;
            ::std::shared_ptr<ModPath> searchModule;
        };

        // NOTE: Use a unique pointer to reduce the size to 1 pointer (instead of 5)
        // - Used quite a bit, and parse sometimes runs out of stack.
        ::std::unique_ptr<Inner> inner;

        Hygiene(unsigned int index);

        Inner* operator->() {
            return &*inner;
        }

        const Inner* operator->() const {
            return &*inner;
        }

    public:
        Hygiene();

        Hygiene(const Hygiene& x);

        Hygiene& operator=(const Hygiene& x);

        Hygiene(Hygiene&& x);

        Hygiene& operator=(Hygiene&& x);

        static Hygiene newScope() {
            return Hygiene(++gNextScope);
        }

        static Hygiene newScopeChained(const Hygiene& parent, unsigned int macroDefinition = 0);

        Hygiene withTailScope(const Hygiene& scope, bool inheritModPath = false) const;

        Hygiene getParent() const;

        bool leaveMacroDefinition(unsigned int definition, const Hygiene& tokenContext, const Hygiene& definitionContext);

        bool hasModPath() const {
            return inner->searchModule != 0;
        }

        const ModPath& modPath() const;

        void setModPath(ModPath p) {
            inner->searchModule.reset(new ModPath(::std::move(p)));
        }

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
