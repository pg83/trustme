#pragma once

#include <map>
#include <memory>
#include "hir_hir.h"
#include "hir_type_ref.h"
#include "hir_path.h"
#include "hir_generic_params.h"
#include "range_vec_map.h"

struct TraitResolveCommon {
    const ::HIR::Crate& m_crate;

    const ::HIR::GenericParams* m_impl_generics;
    const ::HIR::GenericParams* m_item_generics;

    struct CachedEquality {
        ::HIR::GenericParams hrbs;
        ::HIR::TypeRef ty;
    };

    ::std::map<::HIR::TypeRef, CachedEquality> m_type_equalities;

    // A pre-calculated list of trait bounds
    struct CachedBound {
        HIR::GenericParams hrbs;
        const HIR::Trait* trait_ptr;
        HIR::TraitPath::assoc_list_t assoc;
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

    typedef RangeVecMap<std::pair<::HIR::TypeRef, ::HIR::GenericPath>, CachedBound, CachedBoundCmp> cached_bounds_t;
    cached_bounds_t m_trait_bounds;

    ::HIR::SimplePath m_lang_Copy;
    ::HIR::SimplePath m_lang_Clone; // 1.29
    ::HIR::SimplePath m_lang_Drop;
    ::HIR::SimplePath m_lang_Sized;
    ::HIR::SimplePath m_lang_Unsize;
    ::HIR::SimplePath m_lang_Fn;
    ::HIR::SimplePath m_lang_FnMut;
    ::HIR::SimplePath m_lang_FnOnce;
    ::HIR::SimplePath m_lang_Box;
    ::HIR::SimplePath m_lang_PhantomData;
    ::HIR::SimplePath m_lang_Generator;        // 1.39
    ::HIR::SimplePath m_lang_DiscriminantKind; // 1.54
    ::HIR::SimplePath m_lang_Pointee;          // 1.54
    ::HIR::SimplePath m_lang_DynMetadata;      // 1.54
    ::HIR::SimplePath m_lang_PointeeSized;     // 1.90
    ::HIR::SimplePath m_lang_MetaSized;        // 1.90
    ::HIR::SimplePath m_lang_Destruct;         // 1.90
    ::HIR::SimplePath m_lang_Future;           // 1.90 (well, added earlier)

    TraitResolveCommon(const ::HIR::Crate& crate);

    bool has_self() const {
        return m_impl_generics ? true : false;
    }

    const ::HIR::GenericParams& impl_generics() const;

    const ::HIR::GenericParams& item_generics() const;

    /// <summary>
    /// Obtain the type for a given constant parameter
    /// </summary>
    const ::HIR::TypeData* get_const_param_type(const Span& sp, unsigned binding) const;

    void prep_indexes(const Span& sp);

protected:
    void prep_indexes__add_equality(const Span& sp, const ::HIR::GenericParams* hrtbs, ::HIR::TypeRef long_ty, ::HIR::TypeRef short_ty);
    void prep_indexes__add_trait_bound(const Span& sp, const ::HIR::GenericParams* hrtbs, ::HIR::TypeRef type, ::HIR::TraitPath trait_path, bool add_parents = true);

    /// Iterate over in-scope bounds (function then type)
    bool iterate_bounds(::std::function<bool(const ::HIR::GenericBound&)> cb) const;
};

extern ::std::ostream& operator<<(::std::ostream& s, const TraitResolveCommon::CachedEquality& x);
