#pragma once

#include "hir_type_ref.h"

#include <functional>

class HIRTypeImpl;

namespace stl {
    class ObjPool;
}

/// Cross-crate index of inherent (non-trait) methods: one instance per
/// compilation, wired on the WireBoard. The HIR conversion pipeline fills it
/// over the root crate and every extern crate; typeck method resolution
/// reads it.
class HIRInherentCache {
public:
    /// Callback arguments:
    /// `self_ty`: Type for `Self` within the `impl` block
    /// `impl`: TypeImpl containing this method
    typedef ::std::function<void(const HIRTypeData* selfTy, const HIRTypeImpl& impl)> callbackT;

    virtual void insertAll(const Span& sp, const HIRTypeImpl& impl, const HIRSimplePath& langBox) = 0;
    /// Locates methods matching the specified type
    virtual void find(const Span& sp, const RcString& name, const HIRTypeData* ty, tCbResolveType tyRes, callbackT cb) const = 0;

    static HIRInherentCache* create(stl::ObjPool& pool);
};
