#pragma once

#include "hir_hir.h"
#include "range_vec_map.h"
#include "hir_typeck_common.h"
#include "hir_typeck_impl_ref.h"
#include "hir_typeck_resolve_common.h"

enum class MetadataType {
    Unknown,     // Unknown still
    None,        // Sized pointer
    Zero,        // No metadata, but still unsized
    Slice,       // usize metadata
    TraitObject, // VTable pointer metadata
};

std::ostream& operator<<(std::ostream& os, const MetadataType& x);

struct StaticImplCallback {
    virtual bool visit(ImplRef impl, bool isFuzzed) = 0;
};

template <typename F>
struct StaticImplCb final: StaticImplCallback {
    F f;

    explicit StaticImplCb(F f)
        : f(f)
    {
    }

    bool visit(ImplRef impl, bool isFuzzed) override {
        return f(mv$(impl), isFuzzed);
    }
};

struct StaticImplMatchCallback {
    virtual bool visit(HIRPathParams params, HIRCompare cmp) = 0;
};

template <typename F>
struct StaticImplMatchCb final: StaticImplMatchCallback {
    F f;

    explicit StaticImplMatchCb(F f)
        : f(f)
    {
    }

    bool visit(HIRPathParams params, HIRCompare cmp) override {
        return f(mv$(params), cmp);
    }
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

// Definitions generated from hir_typeck_static.tu.
#include "hir_typeck_static_tu.h"

class StaticTraitResolve: public TraitResolveCommon {
    class NextSolverBridge;

    MetadataType selfMetadata = MetadataType::Unknown;
    mutable HIRTypeRefMap<bool> copyCache;
    mutable HIRTypeRefMap<bool> cloneCache;
    mutable HIRTypeRefMap<bool> dropCache;
    // Keyed by the interned UfcsKnown type itself (pointer identity).
    mutable HIRTypeRefMap<HIRTypeRef> atyCache;

    /// Cache key for findImplCheckCrateRaw: the impl side is identified by
    /// the addresses of its (immutable, pool-owned) definition parts, the
    /// destination side by interned pointers. The variable-content part
    /// (destination trait params) lives in the bucket entries.
    struct ImplCheckKey {
        const void* implParamsDef;
        const void* implTraitParams;
        const HIRTypeData* implType;
        const void* desTraitPath;
        const HIRTypeData* desType;

        bool operator<(const ImplCheckKey& x) const {
            if (implParamsDef != x.implParamsDef) {
                return implParamsDef < x.implParamsDef;
            }
            if (implTraitParams != x.implTraitParams) {
                return implTraitParams < x.implTraitParams;
            }
            if (implType != x.implType) {
                return implType < x.implType;
            }
            if (desTraitPath != x.desTraitPath) {
                return desTraitPath < x.desTraitPath;
            }
            return desType < x.desType;
        }
    };

    struct ImplCheckEntry {
        bool hasDesParams;
        HIRPathParams desParams;
        HIRPathParams implParams;
        HIRCompare result;
    };

    /// Cache of the result of find_impl__check_crate_raw
    mutable ::std::map<ImplCheckKey, ThinVector<ImplCheckEntry>> cachedImplChecks;
    mutable ::std::vector<::std::tuple<const HIRSimplePath*, const HIRPathParams*, const HIRTypeData*>> findImplStack;
    // Owned by the crate ObjPool and reused across all fully-static goals.
    mutable NextSolverBridge* nextSolver = nullptr;

public:
    explicit StaticTraitResolve(const WireBoard& wb);

private:
    void prepIndexes();

public:
    /// \brief State manipulation
    /// \{
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

    // Used by ResolveUFCS to regenerate
    void prepIndexes(const Span& sp) {
        TraitResolveCommon::prepIndexes(sp);
    }

    /// \}

    /// \brief Lookups
    /// \{
    bool findImplCb(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams& traitParams, const HIRTypeData* type, StaticImplCallback& foundCb) const {
        return this->findImplCb(sp, traitPath, &traitParams, type, foundCb);
    }

    bool findImplCb(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, StaticImplCallback& foundCb, bool dontHandoffToSpecialised = false) const;

    template <typename F>
    bool findImpl(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams& traitParams, const HIRTypeData* type, F f) const {
        StaticImplCb<F> cb(f);
        return findImplCb(sp, traitPath, traitParams, type, cb);
    }

    template <typename F>
    bool findImpl(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, F f, bool dontHandoffToSpecialised = false) const {
        StaticImplCb<F> cb(f);
        return findImplCb(sp, traitPath, traitParams, type, cb, dontHandoffToSpecialised);
    }

private:
    bool findImplBoundsCb(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, StaticImplCallback& foundCb) const;
    bool findImplCheckCrateCb(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, StaticImplCallback& foundCb, const HIRTraitImpl& impl) const;
    bool findImplCheckCrateRawCb(const Span& sp, const HIRSimplePath& desTraitPath, const HIRPathParams* desTraitParams, const HIRTypeData* desType, const HIRGenericParams& implParamsDef, const HIRPathParams& implTraitParams, const HIRTypeData* implType, StaticImplMatchCallback& foundCb) const;

    template <typename F>
    bool findImplBounds(const Span& sp, const HIRSimplePath& traitPath, const HIRPathParams* traitParams, const HIRTypeData* type, F f) const {
        StaticImplCb<F> cb(f);
        return findImplBoundsCb(sp, traitPath, traitParams, type, cb);
    }

    template <typename F>
    bool findImplCheckCrateRaw(const Span& sp, const HIRSimplePath& desTraitPath, const HIRPathParams* desTraitParams, const HIRTypeData* desType, const HIRGenericParams& implParamsDef, const HIRPathParams& implTraitParams, const HIRTypeData* implType, F f) const {
        StaticImplMatchCb<F> cb(f);
        return findImplCheckCrateRawCb(sp, desTraitPath, desTraitParams, desType, implParamsDef, implTraitParams, implType, cb);
    }
    HIRCompare checkAutoTraitImplDestructure(const Span& sp, const HIRSimplePath& trait, const HIRPathParams* paramsPtr, const HIRTypeData* type) const;
    bool typeNeedsAsyncDropInner(const Span& sp, const HIRTypeData* ty, HIRTypeRefSet& stack) const;

public:
    const HIRTypeData* fixTraitDefaultReturn(const Span& sp, const HIRItemPath& p, const HIRTypeData* tpl, HIRTypeRef& tmp) const;

    void expandAssociatedTypes(const Span& sp, HIRTypeRef& input) const;
    void revealOpaqueTypes(const Span& sp, HIRTypeRef& input) const;
    void revealOpaqueTypesPath(const Span& sp, HIRPath& input) const;
    void expandAssociatedTypesPath(const Span& sp, HIRPath& input) const;
    void evaluateArraySize(const Span& sp, HIRArraySize& size) const;
    void evaluateConstGeneric(const Span& sp, HIRConstGeneric& value) const;
    void evaluatePathParams(const Span& sp, HIRPathParams& params) const;
    bool expandAssociatedTypesSingle(const Span& sp, HIRTypeRef& input) const;
    bool typesEqualResolvingOpaque(const Span& sp, const HIRTypeData* left, const HIRTypeData* right) const;

    // Helper: Run monomorphise+EAT if the type contains generics
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
    /// \}

    /// Locate a named trait in the provied trait (either itself or as a parent trait)
    bool findNamedTraitInTraitCb(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* selfType, StaticNamedTraitCallback& callback) const;

    template <typename F>
    bool findNamedTraitInTrait(const Span& sp, const HIRSimplePath& des, const HIRPathParams& params, const HIRTrait& traitPtr, const HIRSimplePath& traitPath, const HIRPathParams& pp, const HIRTypeData* selfType, F f) const {
        StaticNamedTraitCb<F> cb(f);
        return findNamedTraitInTraitCb(sp, des, params, traitPtr, traitPath, pp, selfType, cb);
    }
    ///
    bool traitContainsType(const Span& sp, const HIRGenericPath& traitPath, const HIRTrait& traitPtr, const char* name, HIRGenericPath& outPath) const;
    bool iterateAtyBoundsCb(const Span& sp, const HIRPath::Data::Data_UfcsKnown& pe, StaticTraitPathCallback& cb) const;

    template <typename F>
    bool iterateAtyBounds(const Span& sp, const HIRPath::Data::Data_UfcsKnown& pe, F f) const {
        StaticTraitPathCb<F> cb(f);
        return iterateAtyBoundsCb(sp, pe, cb);
    }

    // --------------
    // Common bounds
    // -------------
    bool typeIsCopy(const Span& sp, const HIRTypeData* ty) const;
    bool typeIsClone(const Span& sp, const HIRTypeData* ty) const; // 1.29
    bool typeIsSized(const Span& sp, const HIRTypeData* ty) const;
    bool typeIsImpossible(const Span& sp, const HIRTypeData* ty) const;
    bool canUnsize(const Span& sp, const HIRTypeData* dst, const HIRTypeData* src) const;
    /// Check if the passed type contains an UnsafeCell (i.e. is interior mutable)
    /// Returns:
    /// - `Fuzzy` if generic (can't know for sure yet)
    /// - `Equal` if it does contain an UnsafeCell
    //  - `Unequal` if it doesn't (shared=immutable)
    HIRCompare typeIsInteriorMutable(const Span& sp, const HIRTypeData* ty) const;

    MetadataType metadataType(const Span& sp, const HIRTypeData* ty, bool errOnUnknown = false) const;

    /// Returns `true` if the passed type either implements Drop, or contains a type that implements Drop
    bool typeNeedsDropGlue(const Span& sp, const HIRTypeData* ty) const;

    /// Resolve a concrete `AsyncDrop::drop` method and its returned future.
    bool findAsyncDrop(const Span& sp, const HIRTypeData* ty, HIRPath& path, HIRTypeRef& futureTy) const;

    /// Returns `true` if async drop glue for this concrete type can suspend.
    bool typeNeedsAsyncDrop(const Span& sp, const HIRTypeData* ty) const;

    const HIRTypeData* isTypeOwnedBox(const HIRTypeData* ty) const;
    const HIRTypeData* isTypePhantomData(const HIRTypeData* ty) const;

    HIRTypeRef getFieldType(const Span& sp, const HIRTypeData* ty, const RcString& name) const;

    using ValuePtr = TypeckValuePtr;

    struct ResolvedTraitImplPath {
        HIRTypeRef type;
        HIRPathParams traitParams;
    };

    /// `signature_only` - Returns a pointer to an item with the correct signature, not the actual implementation (faster)
    ValuePtr getValue(const Span& sp, const HIRPath& p, MonomorphState& outParams, bool signatureOnly = false,
        const HIRGenericParams** outImplParamsDef = nullptr, ResolvedTraitImplPath* outTraitImplPath = nullptr) const;
};
