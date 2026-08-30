#pragma once

#include "hir_type_ref.h"

class HIRTypeImpl;

namespace stl {
    class ObjPool;
}

class HIRInherentCache {
public:
    struct Callback {
        virtual void visit(const HIRType* selfTy, const HIRTypeImpl& impl) = 0;
    };

    template <typename F>
    struct Cb final: Callback {
        F f;

        explicit Cb(F f)
            : f(f)
        {
        }

        void visit(const HIRType* selfTy, const HIRTypeImpl& impl) override {
            f(selfTy, impl);
        }
    };

    virtual void insertAll(const Span& sp, const HIRTypeImpl& impl, const HIRSimplePath& langBox) = 0;

    virtual void findWith(const Span& sp, const RcString& name, const HIRType* ty, tCbResolveType tyRes, Callback& cb) const = 0;

    template <typename F>
    void find(const Span& sp, const RcString& name, const HIRType* ty, tCbResolveType tyRes, F f) const {
        Cb<F> cb(f);
        findWith(sp, name, ty, tyRes, cb);
    }

    static HIRInherentCache* create(stl::ObjPool& pool);
};
