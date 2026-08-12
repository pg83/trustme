#pragma once

#include "span.h"
#include "rc_string.h"

#include <memory>
#include <vector>
#include <cstdint>

class HIRTypeData;
using HIRTypeRef = const HIRTypeData*;
class HIRTypeInterner;

struct HIRGenericRef;
struct HIRLifetimeRef;
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

class HIRTrackHrbStack {
    mutable std::vector<const HIRGenericParams*> hrbStack;

public:
    class PopOnDrop {
        friend class HIRTrackHrbStack;
        std::vector<const HIRGenericParams*>* v;

        PopOnDrop();
        explicit PopOnDrop(std::vector<const HIRGenericParams*>& v);

    public:
        ~PopOnDrop();

        PopOnDrop(const PopOnDrop&) = delete;
        PopOnDrop(PopOnDrop&& x);
    };

    PopOnDrop pushHrb(const std::unique_ptr<HIRGenericParams>& params) const;
    PopOnDrop pushHrb(const HIRGenericParams& params) const;

    const HIRGenericParams* hasHrb() const {
        return hrbStack.empty() ? nullptr : hrbStack.back();
    }
};

class HIRMatchGenerics: virtual public HIRTrackHrbStack {
public:
    HIRCompare cmpPath(const Span& sp, const HIRPath& tyL, const HIRPath& tyR, tCbResolveType resolveCb);
    virtual HIRCompare cmpType(const Span& sp, const HIRTypeData* tyL, const HIRTypeData* tyR, tCbResolveType resolveCb);

    virtual HIRCompare matchTy(const HIRGenericRef& g, const HIRTypeData* ty, tCbResolveType resolveCb) = 0;
    virtual HIRCompare matchVal(const HIRGenericRef& g, const HIRConstGeneric& sz) = 0;

    virtual HIRCompare matchLft(const HIRGenericRef&, const HIRLifetimeRef&);
};

enum class HIRInferClass {
    None,
    Integer,
    Float,
};

enum class HIRCoreType;
enum class HIRBorrowType;
struct HIRTypeDataFunctionPointer;
class HIRTypePathBinding;
