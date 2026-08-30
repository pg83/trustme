#pragma once

#include "rc_string.h"

#include <std/lib/vector.h>

#include <memory>
#include <string>
#include <vector>

namespace stl {
    class ObjPool;
    class ZeroCopyOutput;
}

struct Ident {
    // TODO: Use AST::AbsolutePath instead
    struct ModPath {
        RcString crate;
        stl::Vector<RcString> ents;
    };

    // TODO: make this a reference-counted pointer instead (so it's cheaper to copy)

    class Hygiene {
    public:
        struct Inner {
            stl::Vector<unsigned int> contexts;

            stl::Vector<unsigned int> macroDefinitions;
            std::shared_ptr<ModPath> searchModule;
        };

    private:
        const Inner* inner = nullptr;

        explicit Hygiene(const Inner* inner)
            : inner(inner)
        {
        }

        Inner clone() const;
        static const Inner* store(stl::ObjPool& pool, Inner v);

    public:
        Hygiene() = default;
        Hygiene(const Hygiene& x) = default;
        Hygiene& operator=(const Hygiene& x) = default;
        Hygiene(Hygiene&& x) = default;
        Hygiene& operator=(Hygiene&& x) = default;

        static Hygiene newScope(u32& id, stl::ObjPool& pool);

        static Hygiene newScopeChained(u32& id, stl::ObjPool& pool, const Hygiene& parent, unsigned int macroDefinition = 0, bool itemOpaque = false);

        Hygiene withTailScope(stl::ObjPool& pool, const Hygiene& scope, bool inheritModPath = false) const;

        Hygiene getParent(stl::ObjPool& pool) const;

        bool leaveMacroDefinition(stl::ObjPool& pool, unsigned int definition, const Hygiene& tokenContext, const Hygiene& definitionContext);

        bool hasModPath() const {
            return inner && inner->searchModule;
        }

        const ModPath& modPath() const;

        void setModPath(stl::ObjPool& pool, ModPath p);

        bool isVisible(const Hygiene& source) const;

        RcString applyToItemName(const RcString& name) const;

        void fmt(stl::ZeroCopyOutput& out) const;

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
    };

    Hygiene hygiene;
    RcString name;

    bool isRaw = false;

    Ident(const char* name);

    Ident(RcString name);

    Ident(Hygiene hygiene, RcString name);

    Ident(Ident&& x) = default;
    Ident(const Ident& x) = default;
    Ident& operator=(Ident&& x) = default;
    Ident& operator=(const Ident& x) = default;

    RcString intoString() {
        return std::move(name);
    }

    RcString hygienicName() const {
        return hygiene.applyToItemName(name);
    }

    bool operator==(const char* s) const {
        return this->name == s;
    }

    bool sameName(const Ident& x) const {
        return this->name == x.name;
    }

    bool sameToken(const Ident& x) const {
        return this->name == x.name && this->isRaw == x.isRaw;
    }

    bool operator==(const Ident& x) const {
        return this->name == x.name && this->hygiene == x.hygiene;
    }

    bool operator!=(const Ident& x) const {
        return !(*this == x);
    }

    bool operator<(const Ident& x) const;
};
