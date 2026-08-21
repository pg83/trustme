#pragma once

#include "span.h"
#include "rc_string.h"

#include <memory>
#include <vector>
#include <cstdint>
#include <set> // escape: uid-ordered alias below, single declaration point
#include <map> // escape: uid-ordered alias below, single declaration point

class HIRTypeData;
using HIRTypeRef = const HIRTypeData*;
class HIRTypeInterner;

// Interned types are ordered by the interner-assigned uid (creation order):
// deterministic across runs and allocation-layout changes. The ordering
// itself is the ord(const HIRTypeData*, const HIRTypeData*) overload in
// common.h; it also drives HIRPath::ord and the pair/vector ord templates.
struct HIRTypeUidOrder {
    bool operator()(const HIRTypeData* a, const HIRTypeData* b) const {
        return ord(a, b) == OrdLess;
    }
};

// The only sanctioned ordered containers over HIRTypeRef: iteration follows
// type-creation order, never pointer order (pointer order leaks the
// allocation layout into the emitted output).
using HIRTypeRefSet = ::std::set<HIRTypeRef, HIRTypeUidOrder>; // escape: deterministic-order alias, replaces address-ordered sets across the codebase
template <typename V>
using HIRTypeRefMap = ::std::map<HIRTypeRef, V, HIRTypeUidOrder>; // escape: deterministic-order alias, replaces address-ordered maps across the codebase

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
public:
    HIRCompare cmpPath(const Span& sp, const HIRPath& tyL, const HIRPath& tyR, tCbResolveType resolveCb);
    virtual HIRCompare cmpType(const Span& sp, const HIRTypeData* tyL, const HIRTypeData* tyR, tCbResolveType resolveCb);

    virtual HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) = 0;
    virtual HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) = 0;
};

enum class HIRInferClass {
    None,
    Integer,
    Float,
};

// Alias expansion happens before expression type checking has an inference
// table. Tagged placeholders retain the identity of one alias argument until
// HMTypeInferrence replaces every occurrence with the same body-local ivar.
constexpr unsigned HIR_INFER_ALIAS_INPUT_MIN = 1u << 31;
inline bool isAliasInputInfer(unsigned index) {
    return index >= HIR_INFER_ALIAS_INPUT_MIN && index != ~0u;
}

enum class HIRCoreType;
enum class HIRBorrowType;
struct HIRTypeDataFunctionPointer;
class HIRTypePathBinding;
