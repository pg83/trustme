#pragma once

#include <map>
#include <memory>
#include "hir_hir.h"
#include "hir_type_ref.h"
#include "hir_path.h"
#include "hir_generic_params.h"
#include "range_vec_map.h"

struct TraitResolveCommon {
    const ::HIR::Crate& crate;

    const ::HIR::GenericParams* implGenerics;
    const ::HIR::GenericParams* itemGenerics;

    struct CachedEquality {
        ::HIR::GenericParams hrbs;
        ::HIR::TypeRef ty;
    };

    ::std::map<::HIR::TypeRef, CachedEquality> typeEqualities;

    // A pre-calculated list of trait bounds
    struct CachedBound {
        HIR::GenericParams hrbs;
        const HIR::Trait* trait_ptr;
        HIR::TraitPath::assocListT assoc;
        HIR::BoundConstness constness = HIR::BoundConstness::Never;
    };

    struct CachedBoundCmp {
        typedef std::pair<::HIR::TypeRef, ::HIR::GenericPath> key_t;
        typedef std::pair<const ::HIR::TypeData*, const ::HIR::GenericPath&> ref_t;
        typedef std::pair<const ::HIR::TypeData*, const ::HIR::SimplePath&> ref_sp_t;

        Ordering ord(const key_t& a, const key_t& b) const {
            return ::ord(a, b);
        }

        bool operator()(const key_t& a, const key_t& b) const {
            return ord(a, b) == OrdLess;
        }

        Ordering ord(const key_t& a, const ref_t& b) const;

        bool operator()(const key_t& a, const ref_t& b) const {
            return ord(a, b) == OrdLess;
        }

        bool operator()(const ref_t& a, const key_t& b) const {
            return ord(b, a) == OrdGreater;
        }

        Ordering ord(const key_t& a, const ref_sp_t& b) const;

        bool operator()(const key_t& a, const ref_sp_t& b) const {
            return ord(a, b) == OrdLess;
        }

        bool operator()(const ref_sp_t& a, const key_t& b) const {
            return ord(b, a) == OrdGreater;
        }
    };

    typedef RangeVecMap<std::pair<::HIR::TypeRef, ::HIR::GenericPath>, CachedBound, CachedBoundCmp> cachedBoundsT;
    cachedBoundsT traitBounds;

    ::HIR::SimplePath mLangCopy;
    ::HIR::SimplePath mLangClone; // 1.29
    ::HIR::SimplePath mLangDrop;
    ::HIR::SimplePath mLangSized;
    ::HIR::SimplePath mLangUnsize;
    ::HIR::SimplePath mLangFn;
    ::HIR::SimplePath mLangFnMut;
    ::HIR::SimplePath mLangFnOnce;
    ::HIR::SimplePath mLangBox;
    ::HIR::SimplePath mLangPhantomData;
    ::HIR::SimplePath mLangGenerator;        // 1.39
    ::HIR::SimplePath mLangDiscriminantKind; // 1.54
    ::HIR::SimplePath mLangPointee;          // 1.54
    ::HIR::SimplePath mLangDynMetadata;      // 1.54
    ::HIR::SimplePath mLangPointeeSized;     // 1.90
    ::HIR::SimplePath mLangMetaSized;        // 1.90
    ::HIR::SimplePath mLangDestruct;         // 1.90
    ::HIR::SimplePath mLangFuture;           // 1.90 (well, added earlier)

    TraitResolveCommon(const ::HIR::Crate& crate);

    bool has_self() const {
        return implGenerics ? true : false;
    }

    const ::HIR::GenericParams& impl_generics() const;

    const ::HIR::GenericParams& item_generics() const;

    /// <summary>
    /// Obtain the type for a given constant parameter
    /// </summary>
    const ::HIR::TypeData* get_const_param_type(const Span& sp, unsigned binding) const;

    void prep_indexes(const Span& sp);

protected:
    void prepIndexesAddEquality(const Span& sp, const ::HIR::GenericParams* hrtbs, ::HIR::TypeRef long_ty, ::HIR::TypeRef short_ty);
    void prepIndexesAddTraitBound(const Span& sp, const ::HIR::GenericParams* hrtbs, ::HIR::TypeRef type, ::HIR::TraitPath trait_path, bool addParents = true);

    /// Iterate over in-scope bounds (function then type)
    bool iterate_bounds(::std::function<bool(const ::HIR::GenericBound&)> cb) const;
};

extern ::std::ostream& operator<<(::std::ostream& s, const TraitResolveCommon::CachedEquality& x);
