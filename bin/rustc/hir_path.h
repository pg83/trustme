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

    HIRSimplePath(RcString crate, const stl::Vector<RcString>& components);

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

[[noreturn]] void hirParamsListOutOfRange();

template <typename T>
class HIRParamsList {
    const ThinVector<T>* list_ = nullptr;

public:
    HIRParamsList() = default;

    explicit HIRParamsList(const ThinVector<T>* list);

    const ThinVector<T>* identity() const;

    size_t size() const;

    bool empty() const;

    const T& operator[](size_t i) const;

    const T& at(size_t i) const;

    const T& front() const;

    const T& back() const;

    const T* begin() const;

    const T* end() const;

    const T* data() const;
};

template <typename T>
HIRParamsList<T>::HIRParamsList(const ThinVector<T>* list)
    : list_(list)
{
}

template <typename T>
auto HIRParamsList<T>::identity() const -> const ThinVector<T>* {
    return list_;
}

template <typename T>
auto HIRParamsList<T>::size() const -> size_t {
    return list_ ? list_->size() : 0;
}

template <typename T>
auto HIRParamsList<T>::empty() const -> bool {
    return size() == 0;
}

template <typename T>
auto HIRParamsList<T>::operator[](size_t i) const -> const T& {
    return (*list_)[i];
}

template <typename T>
auto HIRParamsList<T>::at(size_t i) const -> const T& {
    if (i >= size()) {
        hirParamsListOutOfRange();
    }
    return (*list_)[i];
}

template <typename T>
auto HIRParamsList<T>::front() const -> const T& {
    return at(0);
}

template <typename T>
auto HIRParamsList<T>::back() const -> const T& {
    return at(size() - 1);
}

template <typename T>
auto HIRParamsList<T>::begin() const -> const T* {
    return list_ ? list_->begin() : nullptr;
}

template <typename T>
auto HIRParamsList<T>::end() const -> const T* {
    return list_ ? list_->end() : nullptr;
}

template <typename T>
auto HIRParamsList<T>::data() const -> const T* {
    return begin();
}

u64 hirConstGenericExactHash(const HIRConstGeneric& value);
bool hirConstGenericExactEqual(const HIRConstGeneric& a, const HIRConstGeneric& b);

struct HIRPathParamsBuilder;

struct HIRPathParams {
    HIRParamsList<const HIRType*> types;
    HIRParamsList<HIRConstGeneric> values;

    HIRPathParams() = default;
    HIRPathParams(const HIRType*);
    HIRPathParams(const HIRPathParamsBuilder& builder);
    HIRPathParams(HIRPathParamsBuilder&& builder);

    static HIRPathParams fromView(const HIRType* const* types, size_t typeCount, const HIRConstGeneric* values, size_t valueCount);
    static HIRPathParams fromTypes(const HIRType* const* types, size_t count);

    HIRPathParams clone() const;
    HIRPathParams withTypes(const HIRType* const* types) const;
    HIRPathParams withType(size_t index, const HIRType* type) const;
    HIRPathParams appended(const HIRType* type) const;
    HIRPathParams appendedValue(HIRConstGeneric value) const;

    template <typename F, typename G>
    HIRPathParams map(F typeFn, G valueFn) const;

    template <typename F>
    HIRPathParams mapTypes(F typeFn) const;

    bool sameAs(const HIRPathParams& x) const;

    HIRCompare compareWithPlaceholders(const Span& sp, const HIRPathParams& x, tCbResolveType resolvePlaceholder) const;
    HIRCompare matchTestGenericsFuzz(const Span& sp, const HIRPathParams& x, tCbResolveType resolvePlaceholder, HIRMatchGenerics& match) const;
    bool equalsIgnoringRegions(const HIRPathParams& x) const;

    bool hasParams() const;

    bool operator==(const HIRPathParams& x) const;

    bool operator!=(const HIRPathParams& x) const;

    bool operator<(const HIRPathParams& x) const;

    Ordering ord(const HIRPathParams& x) const;
};

struct HIRPathParamsBuilder {
    ThinVector<const HIRType*> types;
    ThinVector<HIRConstGeneric> values;

    HIRPathParamsBuilder() = default;

    explicit HIRPathParamsBuilder(const HIRPathParams& base);
};

template <typename F, typename G>
HIRPathParams HIRPathParams::map(F typeFn, G valueFn) const {
    if (types.size() <= 16 && values.size() <= 4) {
        const HIRType* foldedTypes[16];
        HIRConstGeneric foldedValues[4];
        bool changed = false;
        for (size_t i = 0; i < types.size(); i++) {
            foldedTypes[i] = typeFn(types[i]);
            changed |= foldedTypes[i] != types[i];
        }
        for (size_t i = 0; i < values.size(); i++) {
            foldedValues[i] = valueFn(values[i]);
            changed |= !hirConstGenericExactEqual(foldedValues[i], values[i]);
        }
        return changed ? fromView(foldedTypes, types.size(), foldedValues, values.size()) : *this;
    }
    HIRPathParamsBuilder builder;
    builder.types.reserve(types.size());
    for (const auto* type : types) {
        builder.types.push_back(typeFn(type));
    }
    builder.values.reserve(values.size());
    for (const auto& value : values) {
        builder.values.push_back(valueFn(value));
    }
    return HIRPathParams(std::move(builder));
}

template <typename F>
HIRPathParams HIRPathParams::mapTypes(F typeFn) const {
    return map(typeFn, [](const HIRConstGeneric& value) { return value.clone(); });
}


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

size_t hirPathTypeChildren(const HIRPath& path, const HIRType** out, size_t capacity);

bool hirPathHasValues(const HIRPath& path);

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

const HIRConstGenericUnevaluated* internUnevaluated(HIRConstGenericUnevaluated value);
HIRPath hirPathWithChildren(const HIRPath& shape, const HIRType* const* children);
