#pragma once

#include "span.h"
#include "common.h"
#include "output.h"
#include "thin_vector.h"
#include "hir_expr_ptr.h"
#include "hir_type_ref.h"
#include "hir_generic_ref.h"

#include <std/lib/vector.h>

#include <span>

struct EncodedLiteral;
class Monomorphiser;

namespace stl {
    class ObjPool;
}

enum class HIRBoundConstness : u8 {
    Never,
    Always,
    Maybe,
};

const EncodedLiteral* freezeEncodedLiteral(stl::ObjPool& pool, EncodedLiteral e);
struct HIRConstGenericUnevaluated;

struct HIRInferData {
    unsigned index;

    HIRInferData(unsigned index = ~0u)
        : index(index)
    {
    }
};

#include "hir_path_tu.h"
class HIRTrait;
class HIRGenericParams;

HIRCompare& operator&=(HIRCompare& x, const HIRCompare& y);

struct HIRSimplePathData {
    u64 hash1;
    u64 hash2;
    ThinVector<RcString> members;
};

struct HIRSimplePath {
private:
    const HIRSimplePathData* p;

    HIRSimplePath(const HIRSimplePathData* p)
        : p(p)
    {
    }

    HIRSimplePath(ThinVector<RcString> members);

public:
    HIRSimplePath();

    HIRSimplePath(RcString crate);

    HIRSimplePath(RcString crate, stl::Vector<RcString> components);

    HIRSimplePath(RcString crate, std::span<RcString> components);

    HIRSimplePath(RcString crate, std::span<const RcString> components);

    HIRSimplePath(RcString crate, std::initializer_list<RcString> components);

    HIRSimplePath clone() const {
        return *this;
    }

    const HIRSimplePathData* rawData() const {
        return p;
    }

    HIRSimplePath parent() const;

    RcString crateName() const;

    std::span<const RcString> components() const {
        if (!p) {
            return {};
        }
        const auto& m = p->members;
        return m.empty() ? std::span<const RcString>() : std::span<const RcString>(m.begin() + 1, m.end());
    }

    stl::Vector<RcString> componentsVec() const;

    HIRSimplePath operator+(const RcString& s) const;

    void operator+=(const RcString& s);
    RcString popComponent();

    void updateCrateName(RcString v);
    void updateLastComponent(RcString v);

    bool operator==(const HIRSimplePath& x) const {
        return p == x.p;
    }

    bool operator!=(const HIRSimplePath& x) const {
        return p != x.p;
    }

    bool operator<(const HIRSimplePath& x) const {
        return ord(x) == OrdLess;
    }

    Ordering ord(const HIRSimplePath& x) const {
        if (p == x.p) {
            return OrdEqual;
        }
        if (!p) {
            return OrdLess;
        }
        if (!x.p) {
            return OrdGreater;
        }
        return ::ord(p->members, x.p->members);
    }

    bool startsWith(const HIRSimplePath& x, bool skipLast = false) const;
};

struct HIRPathParams {
    ThinVector<const HIRType*> types;
    ThinVector<HIRConstGeneric> values;

    HIRPathParams();
    HIRPathParams(const HIRType*);
    HIRPathParams clone() const;
    HIRPathParams(const HIRPathParams&) = delete;
    HIRPathParams& operator=(const HIRPathParams&) = delete;
    HIRPathParams(HIRPathParams&&) = default;
    HIRPathParams& operator=(HIRPathParams&&) = default;

    HIRCompare compareWithPlaceholders(const Span& sp, const HIRPathParams& x, tCbResolveType resolvePlaceholder) const;
    HIRCompare matchTestGenericsFuzz(const Span& sp, const HIRPathParams& x, tCbResolveType resolvePlaceholder, HIRMatchGenerics& match) const;
    bool equalsIgnoringRegions(const HIRPathParams& x) const;

    bool hasParams() const {
        return !types.empty() || !values.empty();
    }

    bool operator==(const HIRPathParams& x) const {
        return ord(x) == OrdEqual;
    }

    bool operator!=(const HIRPathParams& x) const {
        return ord(x) != OrdEqual;
    }

    bool operator<(const HIRPathParams& x) const {
        return ord(x) == OrdLess;
    }

    Ordering ord(const HIRPathParams& x) const;
};

class HIRGenericPath {
public:
    HIRSimplePath path;
    HIRPathParams params;

    HIRGenericPath();
    HIRGenericPath(HIRSimplePath sp);
    HIRGenericPath(HIRSimplePath sp, HIRPathParams params);

    HIRGenericPath clone() const;
    HIRCompare compareWithPlaceholders(const Span& sp, const HIRGenericPath& x, tCbResolveType resolvePlaceholder) const;
    bool equalsIgnoringRegions(const HIRGenericPath& x) const;

    bool operator==(const HIRGenericPath& x) const {
        return ord(x) == OrdEqual;
    }

    bool operator!=(const HIRGenericPath& x) const {
        return ord(x) != OrdEqual;
    }

    bool operator<(const HIRGenericPath& x) const {
        return ord(x) == OrdLess;
    }

    Ordering ord(const HIRGenericPath& x) const;
};

class HIRTraitPath {
public:
    // TODO: Each bound should list its origin trait
    struct AtyEqual {
        HIRGenericPath sourceTrait;
        HIRPathParams atyParams;
        const HIRType* type;

        Ordering ord(const AtyEqual& x) const;

        AtyEqual clone() const {
            return AtyEqual{sourceTrait.clone(), atyParams.clone(), type};
        }
    };

    struct AtyBound {
        HIRGenericPath sourceTrait;
        HIRPathParams atyParams;
        std::vector<HIRTraitPath> traits;

        Ordering ord(const AtyBound& x) const;

        AtyBound clone() const;
    };

    typedef std::map<RcString, AtyEqual> assocListT;

    HIRGenericPath path;
    assocListT typeBounds;
    std::map<RcString, AtyBound> traitBounds;
    HIRBoundConstness constness = HIRBoundConstness::Never;
    const HIRTrait* traitPtr;

    HIRTraitPath();
    explicit HIRTraitPath(HIRGenericPath path);
    HIRTraitPath(HIRGenericPath path, assocListT typeBounds, std::map<RcString, AtyBound> traitBounds, const HIRTrait* traitPtr = nullptr, HIRBoundConstness constness = HIRBoundConstness::Never);
    ~HIRTraitPath();
    HIRTraitPath(HIRTraitPath&&);
    HIRTraitPath& operator=(HIRTraitPath&&);

    HIRTraitPath clone() const;
    HIRCompare compareWithPlaceholders(const Span& sp, const HIRTraitPath& x, tCbResolveType resolvePlaceholder) const;
    bool equalsIgnoringRegions(const HIRTraitPath& x) const;

    bool operator==(const HIRTraitPath& x) const {
        return ord(x) == OrdEqual;
    }

    bool operator!=(const HIRTraitPath& x) const {
        return ord(x) != OrdEqual;
    }

    bool operator<(const HIRTraitPath& x) const {
        return ord(x) == OrdLess;
    }

    Ordering ord(const HIRTraitPath& x) const;
};

#include "hir_path_data_tu.h"

class HIRPath {
public:
    using Data = HIRPathData;

    Data data;

    HIRPath(Data data);

    HIRPath(HIRGenericPath _);
    HIRPath(HIRSimplePath _);

    HIRPath(const HIRType* ty, RcString item, HIRPathParams itemParams = HIRPathParams());
    HIRPath(const HIRType* ty, HIRGenericPath trait, RcString item, HIRPathParams itemParams = HIRPathParams());

    HIRPath clone() const;
    HIRCompare compareWithPlaceholders(const Span& sp, const HIRPath& x, tCbResolveType resolvePlaceholder) const;
    bool equalsIgnoringRegions(const HIRPath& x) const;

    Ordering ord(const HIRPath& x) const;

    bool operator==(const HIRPath& x) const;

    bool operator!=(const HIRPath& x) const {
        return !(*this == x);
    }

    bool operator<(const HIRPath& x) const {
        return ord(x) == OrdLess;
    }
};

struct HIRConstGenericUnevaluated {
    const HIRType* selfType = nullptr;

    HIRPathParams paramsImpl;
    HIRPathParams paramsItem;

    std::shared_ptr<HIRExprPtr> expr;

    HIRConstGenericUnevaluated(HIRExprPtr ep);
    HIRConstGenericUnevaluated clone() const;
    HIRConstGenericUnevaluated monomorph(const Span& sp, const Monomorphiser& ms, bool allowInfer = true) const;
    bool equivalent(const HIRConstGenericUnevaluated& x) const;
    Ordering ord(const HIRConstGenericUnevaluated& x) const;
    void fmt(stl::ZeroCopyOutput& os) const;

private:
    HIRConstGenericUnevaluated();
};
