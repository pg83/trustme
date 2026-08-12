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
        ::std::shared_ptr<AST::Path> inPath;          // Only valid when
        ::std::shared_ptr<AST::AbsolutePath> visPath; // if null, then global
        Ty mTy;

        Visibility();

    public:
        static Visibility make_bare_private();

        static Visibility make_global();
        static Visibility make_restricted(Ty ty, AST::AbsolutePath p);
        static Visibility make_restricted(AST::AbsolutePath p, AST::Path in_path);

        void fmt(::std::ostream& os) const;
        friend std::ostream& operator<<(::std::ostream& os, const Visibility& x);

        Ty ty() const {
            return mTy;
        }

        bool is_global() const {
            return mTy == Ty::Pub;
        }

        const AST::Path& in_path() const;

        const AST::AbsolutePath& vis_path() const;

        bool is_visible(const ::AST::AbsolutePath& from_mod) const;
        /// Returns true if this visibility is "more" than `x`
        bool contains(const Visibility& x) const;

        /// Updates this visibility such that `contains(x)` returns true
        void inplace_union(const Visibility& x);
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
        CachedCfg cached_cfg;
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
            , cached_cfg(CachedCfg::Unknown)
            , data(::std::move(data))
        {
        }
    };

    template <typename T>
    using NamedList = ::std::vector<Named<T>>;

} // namespace AST
