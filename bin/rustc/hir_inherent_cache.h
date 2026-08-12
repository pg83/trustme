#pragma once

#include "range_vec_map.h"
#include "hir_type_ref.h"
#include <map>

namespace HIR {

    class TypeImpl;

    /// <summary>
    /// Cached lookup logic for inherent (non-trait) methods on types
    /// </summary>
    class InherentCache {
    private:
        typedef ::std::function<void(const HIR::TypeData* self_ty, const HIR::TypeImpl& impl)> innerCallbackT;

        struct Lowest {
            // Same as HIR::Crate::ImplGroup
            typedef ::std::vector<const HIR::TypeImpl*> listT;
            ::std::map<::HIR::SimplePath, listT> named;
            listT nonNamed; // TODO: use a map of HIR::TypeRef::Data::Tag
            listT generic;

            void insert(const Span& sp, const HIR::TypeImpl& impl);
            void iterate(const HIR::TypeData* ty, innerCallbackT& cb) const;
        };

        /// <summary>
        /// A layer of the cache
        /// </summary>
        struct Inner {
            /// Cache content used for just `Self`
            Lowest byvalue;
            // Sub-caches for different wrappers around `Self` (can recurse)
            std::unique_ptr<Inner> ref;
            std::unique_ptr<Inner> refMut;
            std::unique_ptr<Inner> refMove;
            std::unique_ptr<Inner> ptr;
            std::unique_ptr<Inner> ptrMut;
            std::unique_ptr<Inner> ptrMove;
            std::map<HIR::SimplePath, Inner> mPath;

            void insert(const Span& sp, const HIR::TypeData* receiver, const HIR::TypeImpl& impl);
            void find(const Span& sp, const HIR::TypeData* curTy, tCbResolveType ty_res, innerCallbackT& cb) const;
        };

        std::map<RcString, Inner> items;

    public:
        /// Callback arguments:
        /// `self_ty`: Type for `Self` within the `impl` block
        /// `impl`: TypeImpl containing this method
        typedef ::std::function<void(const HIR::TypeData* self_ty, const HIR::TypeImpl& impl)> callbackT;

        void insertAll(const Span& sp, const HIR::TypeImpl& impl, const HIR::SimplePath& langBox);
        /// Locates methods matching the specifided type
        void find(const Span& sp, const RcString& name, const HIR::TypeData* ty, tCbResolveType ty_res, callbackT cb) const;
    };

}
