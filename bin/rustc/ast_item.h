#pragma once

#include "output.h"

#include "ast_attrs.h"

#include <string>
#include <vector>

struct ASTAbsolutePath;

class ASTVisibility {
public:
    enum class Ty {
        Private,
        Pub,
        Crate,
        PubCrate,
        PubSuper,
        PubSelf,
        PubIn,
    };

private:
    std::shared_ptr<ASTPath> inPath_;
    std::shared_ptr<ASTAbsolutePath> visPath_;
    Ty ty_;

    ASTVisibility();

public:
    static ASTVisibility makeBarePrivate();

    static ASTVisibility makeGlobal();
    static ASTVisibility makeRestricted(Ty ty, ASTAbsolutePath p);
    static ASTVisibility makeRestricted(ASTAbsolutePath p, ASTPath inPath);

    void fmt(stl::ZeroCopyOutput& os) const;
    Ty ty() const {
        return ty_;
    }

    bool isGlobal() const {
        return ty_ == Ty::Pub;
    }

    const ASTPath& inPath() const;

    const ASTAbsolutePath& visPath() const;

    bool isVisible(const ASTAbsolutePath& fromMod) const;

    bool contains(const ASTVisibility& x) const;

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

    ASTNamed(Span sp, ASTAttributeList attrs, ASTVisibility vis, RcString name, T data)
        : span(sp)
        , attrs(std::move(attrs))
        , vis(std::move(vis))
        , name(std::move(name))
        , cachedCfg(ASTCachedCfg::Unknown)
        , data(std::move(data))
    {
    }
};

template <typename T>
using ASTNamedList = std::vector<ASTNamed<T>>;
