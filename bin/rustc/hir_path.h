#pragma once

#include "span.h"
#include "common.h"
#include "thin_vector.h"
#include "hir_expr_ptr.h"
#include "hir_type_ref.h"
#include "hir_generic_ref.h"

#include <span>

struct EncodedLiteral;
class Monomorphiser;
class HirSerialiser;
class HirDeserialiser;

enum class HIRBoundConstness : u8 {
    Never,
    Always,
    Maybe,
};

class HIREncodedLiteralPtr {
    EncodedLiteral* p;

public:
    ~HIREncodedLiteralPtr();

    HIREncodedLiteralPtr();

    HIREncodedLiteralPtr(EncodedLiteral e);

    HIREncodedLiteralPtr(HIREncodedLiteralPtr&& x);

    HIREncodedLiteralPtr(const HIREncodedLiteralPtr& x) = delete;

    HIREncodedLiteralPtr& operator=(HIREncodedLiteralPtr&& x);

    HIREncodedLiteralPtr& operator=(const HIREncodedLiteralPtr& x) = delete;

    EncodedLiteral& operator*();

    const EncodedLiteral& operator*() const;

    EncodedLiteral* operator->();

    const EncodedLiteral* operator->() const;
};
struct HIRConstGenericUnevaluated;
/// An inference placeholder for a const generic
struct HIRInferData {
    unsigned index;

    HIRInferData(unsigned index = ~0u)
        : index(index)
    {
    }
};

// Definitions generated from hir_path.tu.
#include "hir_path_tu.h"
::std::ostream& operator<<(::std::ostream& os, const HIRConstGeneric& x);

class HIRTrait;
class HIRGenericParams;

::std::ostream& operator<<(::std::ostream& os, const HIRCompare& x);

HIRCompare& operator&=(HIRCompare& x, const HIRCompare& y);

/// Interned storage for HIRSimplePath. Never constructed directly: entries
/// live forever in the process-wide interner in hir_path.cpp.
struct HIRSimplePathData {
    // Two independent Zobrist hashes over (component, position). hash1 keys
    // the interner table; hash2 is what lookups compare instead of the
    // component content. Both support O(1) incremental update on
    // append/pop/replace, which is what makes path mutation cheap.
    u64 hash1;
    u64 hash2;
    ThinVector<RcString> members; // [0] = crate name; empty vector = empty path
};

/// Simple path - Absolute with no generic parameters.
/// De-duplicated: the object is a single pointer into the interner, so copies
/// are trivial and equality is pointer identity. ord() stays lexicographic
/// over the components: HIRSimplePath keys many std::map's whose iteration
/// order leaks into the output, so it must not depend on interning order.
struct HIRSimplePath {
    friend HirSerialiser;
    friend HirDeserialiser;

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

    HIRSimplePath(RcString crate, ::std::vector<RcString> components);

    HIRSimplePath(RcString crate, ::std::span<RcString> components);

    HIRSimplePath(RcString crate, ::std::span<const RcString> components);

    HIRSimplePath(RcString crate, ::std::initializer_list<RcString> components);

    HIRSimplePath clone() const {
        return *this;
    }

    /// The interned identity: stable for the process lifetime, equal iff the
    /// paths are equal. For pointer-keyed caches.
    const HIRSimplePathData* rawData() const {
        return p;
    }

    HIRSimplePath parent() const;

    const RcString& crateName() const;

    ::std::span<const RcString> components() const {
        const auto& m = p->members;
        return m.empty() ? std::span<const RcString>() : std::span<const RcString>(m.begin() + 1, m.end());
    }

    ::std::vector<RcString> componentsVec() const;

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
        return ::ord(p->members, x.p->members);
    }

    bool startsWith(const HIRSimplePath& x, bool skipLast = false) const;
    friend ::std::ostream& operator<<(::std::ostream& os, const HIRSimplePath& x);
};

struct HIRPathParams {
    ThinVector<HIRTypeRef> types;
    ThinVector<HIRConstGeneric> values;

    HIRPathParams();
    HIRPathParams(HIRTypeRef);
    HIRPathParams clone() const;
    HIRPathParams(const HIRPathParams&) = delete;
    HIRPathParams& operator=(const HIRPathParams&) = delete;
    HIRPathParams(HIRPathParams&&) = default;
    HIRPathParams& operator=(HIRPathParams&&) = default;

    HIRCompare compareWithPlaceholders(const Span& sp, const HIRPathParams& x, tCbResolveType resolvePlaceholder) const;
    HIRCompare matchTestGenericsFuzz(const Span& sp, const HIRPathParams& x, tCbResolveType resolvePlaceholder, HIRMatchGenerics& match) const;
    bool equalsIgnoringRegions(const HIRPathParams& x) const;

    /// Indicates that params exist (and thus the target requires monomorphisation)
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

    friend ::std::ostream& operator<<(::std::ostream& os, const HIRPathParams& x);
};

/// Generic path - Simple path with one lot of generic params
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

    friend ::std::ostream& operator<<(::std::ostream& os, const HIRGenericPath& x);
};

class HIRTraitPath {
public:
    // TODO: Each bound should list its origin trait
    struct AtyEqual {
        HIRGenericPath sourceTrait;
        HIRPathParams atyParams;
        HIRTypeRef type;

        Ordering ord(const AtyEqual& x) const;

        AtyEqual clone() const {
            return AtyEqual{sourceTrait.clone(), atyParams.clone(), type};
        }

        friend ::std::ostream& operator<<(::std::ostream& os, const AtyEqual& x);
    };

    /// Associated type trait bounds (`Type: Trait`)
    struct AtyBound {
        HIRGenericPath sourceTrait;
        HIRPathParams atyParams;
        std::vector<HIRTraitPath> traits;

        Ordering ord(const AtyBound& x) const;

        AtyBound clone() const;
    };

    typedef ::std::map<RcString, AtyEqual> assocListT;

    HIRGenericPath path;
    assocListT typeBounds;
    ::std::map<RcString, AtyBound> traitBounds;
    HIRBoundConstness constness = HIRBoundConstness::Never;
    const HIRTrait* traitPtr;

    HIRTraitPath();
    explicit HIRTraitPath(HIRGenericPath path);
    HIRTraitPath(HIRGenericPath path, assocListT typeBounds, ::std::map<RcString, AtyBound> traitBounds, const HIRTrait* traitPtr = nullptr, HIRBoundConstness constness = HIRBoundConstness::Never);
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

    friend ::std::ostream& operator<<(::std::ostream& os, const HIRTraitPath& x);
};

// Definitions generated from hir_path_data.tu.
#include "hir_path_data_tu.h"

class HIRPath {
public:
    using Data = HIRPathData;

    Data data;

    HIRPath(Data data);

    HIRPath(HIRGenericPath _);
    HIRPath(HIRSimplePath _);

    HIRPath(HIRTypeRef ty, RcString item, HIRPathParams itemParams = HIRPathParams());
    HIRPath(HIRTypeRef ty, HIRGenericPath trait, RcString item, HIRPathParams itemParams = HIRPathParams());

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

    friend ::std::ostream& operator<<(::std::ostream& os, const HIRPath& x);
};

struct HIRConstGenericUnevaluated {
    /// `Self` captured by the expression, separate from impl parameters.
    HIRTypeRef selfType = nullptr;
    /// Impl-level parameters to the expression
    HIRPathParams paramsImpl;
    HIRPathParams paramsItem;
    /// HIR/MIR for this unevaluated parameter
    std::shared_ptr<HIRExprPtr> expr;

    HIRConstGenericUnevaluated(HIRExprPtr ep);
    HIRConstGenericUnevaluated clone() const;
    HIRConstGenericUnevaluated monomorph(const Span& sp, const Monomorphiser& ms, bool allowInfer = true) const;
    bool equivalent(const HIRConstGenericUnevaluated& x) const;
    Ordering ord(const HIRConstGenericUnevaluated& x) const;
    void fmt(::std::ostream& os) const;

private:
    HIRConstGenericUnevaluated();
};
