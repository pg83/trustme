#pragma once

#include "hir_hir.h"
#include "hir_path.h"
#include "hir_type_ref.h"
#include "range_vec_map.h"
#include "hir_generic_params.h"

#include <map>
#include <memory>

struct TraitResolveCommon {
    const WireBoard& wb;
    const HIRCrate& crate;

    const HIRGenericParams* mImplGenerics;
    const HIRGenericParams* mItemGenerics;

    struct CachedEquality {
        HIRTypeRef ty;
    };

    ::std::map<HIRTypeRef, CachedEquality> typeEqualities;

    // A pre-calculated list of trait bounds
    struct CachedBound {
        const HIRTrait* traitPtr;
        HIRTraitPath::assocListT assoc;
        HIRBoundConstness constness = HIRBoundConstness::Never;
    };

    struct CachedBoundCmp {
        typedef std::pair<HIRTypeRef, HIRGenericPath> keyT;
        typedef std::pair<const HIRTypeData*, const HIRGenericPath&> refT;
        typedef std::pair<const HIRTypeData*, const HIRSimplePath&> refSpT;

        Ordering ord(const keyT& a, const keyT& b) const {
            return ::ord(a, b);
        }

        bool operator()(const keyT& a, const keyT& b) const {
            return ord(a, b) == OrdLess;
        }

        Ordering ord(const keyT& a, const refT& b) const;

        bool operator()(const keyT& a, const refT& b) const {
            return ord(a, b) == OrdLess;
        }

        bool operator()(const refT& a, const keyT& b) const {
            return ord(b, a) == OrdGreater;
        }

        Ordering ord(const keyT& a, const refSpT& b) const;

        bool operator()(const keyT& a, const refSpT& b) const {
            return ord(a, b) == OrdLess;
        }

        bool operator()(const refSpT& a, const keyT& b) const {
            return ord(b, a) == OrdGreater;
        }
    };

    typedef RangeVecMap<std::pair<HIRTypeRef, HIRGenericPath>, CachedBound, CachedBoundCmp> cachedBoundsT;
    cachedBoundsT traitBounds;

    HIRSimplePath mLangCopy;
    HIRSimplePath mLangClone; // 1.29
    HIRSimplePath mLangDrop;
    HIRSimplePath mLangSized;
    HIRSimplePath mLangUnsize;
    HIRSimplePath mLangFn;
    HIRSimplePath mLangFnMut;
    HIRSimplePath mLangFnOnce;
    HIRSimplePath mLangBox;
    HIRSimplePath mLangPhantomData;
    HIRSimplePath mLangGenerator;        // 1.39
    HIRSimplePath mLangDiscriminantKind; // 1.54
    HIRSimplePath mLangPointee;          // 1.54
    HIRSimplePath mLangDynMetadata;      // 1.54
    HIRSimplePath mLangPointeeSized;     // 1.90
    HIRSimplePath mLangMetaSized;        // 1.90
    HIRSimplePath mLangDestruct;         // 1.90
    HIRSimplePath mLangFuture;           // 1.90 (well, added earlier)

    TraitResolveCommon(const WireBoard& wb);

    bool hasSelf() const {
        return mImplGenerics ? true : false;
    }

    const HIRGenericParams& implGenerics() const;

    const HIRGenericParams& itemGenerics() const;

    /// <summary>
    /// Obtain the type for a given constant parameter
    /// </summary>
    const HIRTypeData* getConstParamType(const Span& sp, unsigned binding) const;

    void prepIndexes(const Span& sp);

protected:
    void prepIndexesAddEquality(const Span& sp, HIRTypeRef longTy, HIRTypeRef shortTy);
    void prepIndexesAddTraitBound(const Span& sp, HIRTypeRef type, HIRTraitPath traitPath, bool addParents = true);

    /// Iterate over in-scope bounds (function then type)
    bool iterateBounds(::std::function<bool(const HIRGenericBound&)> cb) const;
};

extern ::std::ostream& operator<<(::std::ostream& s, const TraitResolveCommon::CachedEquality& x);
