#pragma once

#include <string>
#include <vector>
#include "ast_attrs.h"

namespace AST {

    struct AbsolutePath;

    class Visibility {
    public:
        enum class Ty {
            Private,
            Pub,      // pub
            Crate,    // crate
            PubCrate, // pub(crate)
            PubSuper, // pub(super)
            PubSelf,  // pub(self)
            PubIn,    // pub(in ...)
        };

    private:
        ::std::shared_ptr<AST::Path> mInPath;          // Only valid when
        ::std::shared_ptr<AST::AbsolutePath> mVisPath; // if null, then global
        Ty mTy;

        Visibility();

    public:
        static Visibility makeBarePrivate();

        static Visibility makeGlobal();
        static Visibility makeRestricted(Ty ty, AST::AbsolutePath p);
        static Visibility makeRestricted(AST::AbsolutePath p, AST::Path inPath);

        void fmt(::std::ostream& os) const;
        friend std::ostream& operator<<(::std::ostream& os, const Visibility& x);

        Ty ty() const {
            return mTy;
        }

        bool isGlobal() const {
            return mTy == Ty::Pub;
        }

        const AST::Path& inPath() const;

        const AST::AbsolutePath& visPath() const;

        bool isVisible(const ::AST::AbsolutePath& fromMod) const;
        /// Returns true if this visibility is "more" than `x`
        bool contains(const Visibility& x) const;

        /// Updates this visibility such that `contains(x)` returns true
        void inplaceUnion(const Visibility& x);
    };

    enum class CachedCfg {
        Unknown,
        Yes,
        No,
    };

    template <typename T>
    struct Named {
        Span span;
        AttributeList attrs;
        Visibility vis;
        RcString name;
        CachedCfg cachedCfg;
        T data;

        Named()
            : data()
        {
        }

        //Named(Named&&) = default;
        //Named(const Named&) = default;
        //Named& operator=(Named&&) = default;
        Named(Span sp, AttributeList attrs, Visibility vis, RcString name, T data)
            : span(sp)
            , attrs(::std::move(attrs))
            , vis(::std::move(vis))
            , name(::std::move(name))
            , cachedCfg(CachedCfg::Unknown)
            , data(::std::move(data))
        {
        }
    };

    template <typename T>
    using NamedList = ::std::vector<Named<T>>;

} // namespace AST
