#pragma once

#include "range_vec_map.h"
#include "hir_type_ref.h"
#include <map>


    class HIRTypeImpl;

    /// <summary>
    /// Cached lookup logic for inherent (non-trait) methods on types
    /// </summary>
    class HIRInherentCache {
    private:
        typedef ::std::function<void(const HIRTypeData* selfTy, const HIRTypeImpl& impl)> innerCallbackT;

        struct Lowest {
            // Same as HIR::Crate::ImplGroup
            typedef ::std::vector<const HIRTypeImpl*> listT;
            ::std::map<HIRSimplePath, listT> named;
            listT nonNamed; // TODO: use a map of HIR::TypeRef::Data::Tag
            listT generic;

            void insert(const Span& sp, const HIRTypeImpl& impl);
            void iterate(const HIRTypeData* ty, innerCallbackT& cb) const;
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
            std::map<HIRSimplePath, Inner> mPath;

            void insert(const Span& sp, const HIRTypeData* receiver, const HIRTypeImpl& impl);
            void find(const Span& sp, const HIRTypeData* curTy, tCbResolveType tyRes, innerCallbackT& cb) const;
        };

        std::map<RcString, Inner> items;

    public:
        /// Callback arguments:
        /// `self_ty`: Type for `Self` within the `impl` block
        /// `impl`: TypeImpl containing this method
        typedef ::std::function<void(const HIRTypeData* selfTy, const HIRTypeImpl& impl)> callbackT;

        void insertAll(const Span& sp, const HIRTypeImpl& impl, const HIRSimplePath& langBox);
        /// Locates methods matching the specifided type
        void find(const Span& sp, const RcString& name, const HIRTypeData* ty, tCbResolveType tyRes, callbackT cb) const;
    };

