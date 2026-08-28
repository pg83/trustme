#pragma once

#include "span.h"
#include "rc_string.h"

#include <map>
#include <set>
#include <memory>
#include <vector>
#include <cstdint>

class HIRTypeData;
using HIRTypeRef = const HIRTypeData*;
class HIRTypeInterner;

namespace stl {
    class ObjPool;
}

struct HIRTypeUidOrder {
    bool operator()(const HIRTypeData* a, const HIRTypeData* b) const {
        return ord(a, b) == OrdLess;
    }
};

using HIRTypeRefSet = ::std::set<HIRTypeRef, HIRTypeUidOrder>;
template <typename V>
using HIRTypeRefMap = ::std::map<HIRTypeRef, V, HIRTypeUidOrder>;

struct HIRGenericRef;
struct HIRSimplePath;
class HIRPath;
class HIRConstGeneric;
class HIRGenericParams;

class HIRExprPtr;
struct HIRExprNodeClosure;
struct HIRExprNodeGenerator;
struct HIRExprNodeAsyncBlock;

enum HIRCompare {
    Equal,
    Fuzzy,
    Unequal,
};

class HIRResolvePlaceholders {
public:
    virtual const HIRTypeData* getType(const Span& sp, const HIRTypeData* ty) const = 0;
    virtual const HIRConstGeneric& getVal(const Span& sp, const HIRConstGeneric& v) const = 0;
};

class HIRResolvePlaceholdersNop: public HIRResolvePlaceholders {
    const HIRTypeData* getType(const Span&, const HIRTypeData* ty) const override;

    const HIRConstGeneric& getVal(const Span&, const HIRConstGeneric& v) const override;
};

using tCbResolveType = const HIRResolvePlaceholders&;

class HIRMatchGenerics {
protected:
    struct BorrowMatchedValues {};

    explicit HIRMatchGenerics(BorrowMatchedValues)
        : retainedValuePool(nullptr)
    {
    }

    explicit HIRMatchGenerics(stl::ObjPool& retainedValuePool)
        : retainedValuePool(&retainedValuePool)
    {
    }

public:
    HIRCompare cmpPath(const Span& sp, const HIRPath& tyL, const HIRPath& tyR, tCbResolveType resolveCb);
    virtual HIRCompare cmpType(const Span& sp, const HIRTypeData* tyL, const HIRTypeData* tyR, tCbResolveType resolveCb);

    virtual HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) = 0;
    virtual HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) = 0;

private:
    stl::ObjPool* retainedValuePool;
};

enum class HIRInferClass {
    None,
    Integer,
    Float,
};

constexpr unsigned HIR_INFER_ALIAS_INPUT_MIN = 1u << 31;

inline bool isAliasInputInfer(unsigned index) {
    return index >= HIR_INFER_ALIAS_INPUT_MIN && index != ~0u;
}

constexpr unsigned HIR_INFER_SOLVER_CANONICAL_MIN = 0xF0000000u;

constexpr unsigned HIR_INFER_SOLVER_NORMALIZES_TO_OUTPUT = ~1u;

inline bool isSolverCanonicalInfer(unsigned index) {
    return index >= HIR_INFER_SOLVER_CANONICAL_MIN && index != ~0u;
}

enum class HIRCoreType;
enum class HIRBorrowType;
struct HIRTypeDataFunctionPointer;
class HIRTypePathBinding;
