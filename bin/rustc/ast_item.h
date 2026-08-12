#pragma once

#include <string>
#include <vector>
#include "ast_attrs.h"


    struct ASTAbsolutePath;

    class ASTVisibility {
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
        ::std::shared_ptr<ASTPath> mInPath;          // Only valid when
        ::std::shared_ptr<ASTAbsolutePath> mVisPath; // if null, then global
        Ty mTy;

        ASTVisibility();

    public:
        static ASTVisibility makeBarePrivate();

        static ASTVisibility makeGlobal();
        static ASTVisibility makeRestricted(Ty ty, ASTAbsolutePath p);
        static ASTVisibility makeRestricted(ASTAbsolutePath p, ASTPath inPath);

        void fmt(::std::ostream& os) const;
        friend std::ostream& operator<<(::std::ostream& os, const ASTVisibility& x);

        Ty ty() const {
            return mTy;
        }

        bool isGlobal() const {
            return mTy == Ty::Pub;
        }

        const ASTPath& inPath() const;

        const ASTAbsolutePath& visPath() const;

        bool isVisible(const ASTAbsolutePath& fromMod) const;
        /// Returns true if this visibility is "more" than `x`
        bool contains(const ASTVisibility& x) const;

        /// Updates this visibility such that `contains(x)` returns true
        void inplaceUnion(const ASTVisibility& x);
    };

    enum class ASTCachedCfg {
        Unknown,
        Yes,
        No,
    };

    template <typename T>
    struct ASTNamed {
        Span span;
        ASTAttributeList attrs;
        ASTVisibility vis;
        RcString name;
        ASTCachedCfg cachedCfg;
        T data;

        ASTNamed()
            : data()
        {
        }

        //Named(Named&&) = default;
        //Named(const Named&) = default;
        //Named& operator=(Named&&) = default;
        ASTNamed(Span sp, ASTAttributeList attrs, ASTVisibility vis, RcString name, T data)
            : span(sp)
            , attrs(::std::move(attrs))
            , vis(::std::move(vis))
            , name(::std::move(name))
            , cachedCfg(ASTCachedCfg::Unknown)
            , data(::std::move(data))
        {
        }
    };

    template <typename T>
    using ASTNamedList = ::std::vector<ASTNamed<T>>;

