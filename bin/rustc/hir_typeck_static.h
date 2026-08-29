#pragma once

struct MonomorphState;

#include "hir_hir.h"
#include "range_vec_map.h"
#include "hir_typeck_common.h"
#include "hir_typeck_helpers.h"
#include "hir_typeck_resolve_common.h"

enum class MetadataType {
    Unknown,
    None,
    Zero,
    Slice,
    TraitObject,
};

struct StaticNamedTraitCallback {
    virtual bool visit(const HIRPathParams& params, HIRTraitPath::assocListT assoc) = 0;
};

template <typename F>
struct StaticNamedTraitCb final: StaticNamedTraitCallback {
    F f;

    explicit StaticNamedTraitCb(F f)
        : f(f)
    {
    }

    bool visit(const HIRPathParams& params, HIRTraitPath::assocListT assoc) override {
        return f(params, mv$(assoc));
    }
};

struct StaticTraitPathCallback {
    virtual bool visit(const HIRTraitPath& trait) = 0;
};

template <typename F>
struct StaticTraitPathCb final: StaticTraitPathCallback {
    F f;

    explicit StaticTraitPathCb(F f)
        : f(f)
    {
    }

    bool visit(const HIRTraitPath& trait) override {
        return f(trait);
    }
};

#include "hir_typeck_static_tu.h"

enum class OpaqueReveal {
    UserFacing,
    All,
};

class StaticTraitResolve: public TraitResolveCommon {
    class NextSolverBridge;

    MetadataType selfMetadata = MetadataType::Unknown;
    mutable HIRTypeRefMap<bool> copyCache;
    mutable HIRTypeRefMap<bool> cloneCache;
    mutable HIRTypeRefMap<bool> dropCache;

    mutable HIRTypeRefMap<HIRTypeRef> atyCache;

    OpaqueReveal reveal_ = OpaqueReveal::UserFacing;

    mutable NextSolverBridge* nextSolver = nullptr;

public:
    explicit StaticTraitResolve(const WireBoard& wb, OpaqueReveal reveal = OpaqueReveal::UserFacing);

private:
    void prepIndexes();

public:
    NullOnDrop<const HIRGenericParams> setImplGenerics(HIRStructMarkings::DstType structDstType, const HIRGenericParams& gps);

    NullOnDrop<const HIRGenericParams> setImplGenerics(MetadataType selfMetaType, const HIRGenericParams& gps);

    NullOnDrop<const HIRGenericParams> setImplGenerics(const HIRTypeData* selfTy, const HIRGenericParams& gps);

    void updateImplSelfMetadata(const HIRTypeData* selfTy);

    NullOnDrop<const HIRGenericParams> setItemGenerics(const HIRGenericParams& gps);

    void setImplGenericsRaw(MetadataType selfMetaType, const HIRGenericParams& gps);

    void clearImplGenerics();

    void setItemGenericsRaw(const HIRGenericParams& gps);

    void clearItemGenerics();

    void setBothGenericsRaw(const HIRGenericParams* gpsImpl, const HIRGenericParams* gpsFcn);

    void clearBothGenerics();

    void prepIndexes(const Span& sp) {
        TraitResolveCommon::prepIndexes(sp);
    }

    bool findImplCb(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams& traitParams, const HIRTypeData* type, SolverResponseCallback& foundCb) const {
        return this->findImplCb(sp, traitPath, &traitParams, type, foundCb);
    }

    bool findImplCb(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, SolverResponseCallback& foundCb) const;

    template <typename F>
    bool findImpl(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams& traitParams, const HIRTypeData* type, F f) const {
        SolverResponseCb<F> cb(f);
        return findImplCb(sp, traitPath, traitParams, type, cb);
    }

    template <typename F>
    bool findImpl(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, F f) const {
        SolverResponseCb<F> cb(f);
        return findImplCb(sp, traitPath, traitParams, type, cb);
    }

private:
    bool typeNeedsAsyncDropInner(const Span& sp, const HIRTypeData* ty, HIRTypeRefSet& stack) const;

public:
    const HIRTypeData* fixTraitDefaultReturn(const Span& sp, const HIRItemPath& p, const HIRTypeData* tpl, HIRTypeRef& tmp) const;

    void expandAssociatedTypes(const Span& sp, HIRTypeRef& input) const;
    void revealOpaqueTypes(const Span& sp, HIRTypeRef& input) const;

    void revealOpaqueTypesShallow(const Span& sp, HIRTypeRef& input) const;
    void revealOpaqueTypesPath(const Span& sp, HIRPath& input) const;
    void expandAssociatedTypesPath(const Span& sp, HIRPath& input) const;
    void evaluateArraySize(const Span& sp, HIRArraySize& size) const;
    void evaluateConstGeneric(const Span& sp, HIRConstGeneric& value) const;
    void evaluatePathParams(const Span& sp, HIRPathParams& params) const;
    bool expandAssociatedTypesSingle(const Span& sp, HIRTypeRef& input) const;
    bool typesEqualResolvingOpaque(const Span& sp, const HIRTypeData* left, const HIRTypeData* right) const;

    const HIRTypeData* monomorphExpandOpt(const Span& sp, HIRTypeRef& tmp, const HIRTypeData* input, const Monomorphiser& m) const;

    HIRTypeRef monomorphExpand(const Span& sp, const HIRTypeData* input, const Monomorphiser& m) const;

    void expandAssociatedTypesTp(const Span& sp, HIRTraitPath& input) const;

private:
    void expandAssociatedTypesParams(const Span& sp, HIRPathParams& input) const;
    [[nodiscard]] HIRTypeRef expandAssociatedTypesInner(const Span& sp, HIRTypeRef input) const;
    bool expandAssociatedTypesUfcsInherent(const Span& sp, HIRTypeRef& input) const;
    bool expandAssociatedTypesUfcsKnown(const Span& sp, HIRTypeRef& input, bool recurse = true) const;

protected:
    virtual bool replaceEqualities(HIRTypeRef& input) const;

public:
    bool findNamedTraitInTraitCb(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* selfType, StaticNamedTraitCallback& callback) const;

    template <typename F>
    bool findNamedTraitInTrait(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* selfType, F f) const {
        StaticNamedTraitCb<F> cb(f);
        return findNamedTraitInTraitCb(sp, des, params, traitPtr, traitPath, pp, selfType, cb);
    }

    bool traitContainsType(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const char* name, HIRGenericPath& outPath) const;
    bool iterateAtyBoundsCb(const Span& sp, const HIRPath::Data::Data_UfcsKnown& pe, StaticTraitPathCallback& cb) const;

    template <typename F>
    bool iterateAtyBounds(const Span& sp, const HIRPath::Data::Data_UfcsKnown& pe, F f) const {
        StaticTraitPathCb<F> cb(f);
        return iterateAtyBoundsCb(sp, pe, cb);
    }

    bool typeIsCopy(const Span& sp, const HIRTypeData* ty) const;
    bool typeIsClone(const Span& sp, const HIRTypeData* ty) const;
    bool typeIsSized(const Span& sp, const HIRTypeData* ty) const;
    bool typeIsImpossible(const Span& sp, const HIRTypeData* ty) const;
    bool canUnsize(const Span& sp, const HIRTypeData* dst, const HIRTypeData* src) const;

    HIRCompare typeIsInteriorMutable(const Span& sp, const HIRTypeData* ty) const;

    MetadataType metadataType(const Span& sp, const HIRTypeData* ty, bool errOnUnknown = false) const;

    bool typeNeedsDropGlue(const Span& sp, const HIRTypeData* ty) const;

    bool findAsyncDrop(const Span& sp, const HIRTypeData* ty, HIRPath& path, HIRTypeRef& futureTy) const;

    bool typeNeedsAsyncDrop(const Span& sp, const HIRTypeData* ty) const;

    const HIRTypeData* isTypeOwnedBox(const HIRTypeData* ty) const;
    const HIRTypeData* isTypePhantomData(const HIRTypeData* ty) const;

    HIRTypeRef getFieldType(const Span& sp, const HIRTypeData* ty, const RcString& name) const;

    using ValuePtr = TypeckValuePtr;

    struct ResolvedTraitImplPath {
        HIRTypeRef type;
        HIRPathParams traitParams;
    };

    ValuePtr getValue(const Span& sp, const HIRPath& p, MonomorphState& outParams, bool signatureOnly = false, const HIRGenericParams** outImplParamsDef = nullptr, ResolvedTraitImplPath* outTraitImplPath = nullptr) const;
};
