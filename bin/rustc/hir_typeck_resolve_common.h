#pragma once

#include "hir_hir.h"
#include "hir_path.h"
#include "lang_items.h"
#include "wire_board.h"
#include "hir_type_ref.h"
#include "range_vec_map.h"
#include "hir_generic_params.h"

#include <map>
#include <memory>

struct HIRTypeEqualityCallback {
    virtual const HIRType* visit(const HIRType* type) = 0;
};

template <typename F>
struct HIRTypeEqualityCb final: HIRTypeEqualityCallback {
    F f;

    explicit HIRTypeEqualityCb(F f)
        : f(f)
    {
    }

    const HIRType* visit(const HIRType* type) override {
        return f(type);
    }
};
struct HIRGenericBoundCallback {
    virtual bool visit(const HIRGenericBound& bound) = 0;
};

template <typename F>
struct HIRGenericBoundCb final: HIRGenericBoundCallback {
    F f;

    explicit HIRGenericBoundCb(F f)
        : f(f)
    {
    }

    bool visit(const HIRGenericBound& bound) override {
        return f(bound);
    }
};

struct TraitResolveCommon {
    const WireBoard& wb;
    const HIRCrate& crate;

    const HIRSimplePath& langCopy() const {
        return wb.langItems->copy();
    }

    const HIRSimplePath& langClone() const {
        return wb.langItems->clone();
    }

    const HIRSimplePath& langDrop() const {
        return wb.langItems->drop();
    }

    const HIRSimplePath& langSized() const {
        return wb.langItems->sized();
    }

    const HIRSimplePath& langUnsize() const {
        return wb.langItems->unsize();
    }

    const HIRSimplePath& langFn() const {
        return wb.langItems->fn();
    }

    const HIRSimplePath& langFnMut() const {
        return wb.langItems->fnMut();
    }

    const HIRSimplePath& langFnOnce() const {
        return wb.langItems->fnOnce();
    }

    const HIRSimplePath& langAsyncFn() const {
        return wb.langItems->asyncFn();
    }

    const HIRSimplePath& langAsyncFnMut() const {
        return wb.langItems->asyncFnMut();
    }

    const HIRSimplePath& langAsyncFnOnce() const {
        return wb.langItems->asyncFnOnce();
    }

    const HIRSimplePath& langBox() const {
        return wb.langItems->box();
    }

    const HIRSimplePath& langPhantomData() const {
        return wb.langItems->phantomData();
    }

    const HIRSimplePath& langGenerator() const {
        return wb.langItems->generator();
    }

    const HIRSimplePath& langDiscriminantKind() const {
        return wb.langItems->discriminantKind();
    }

    const HIRSimplePath& langPointee() const {
        return wb.langItems->pointee();
    }

    const HIRSimplePath& langDynMetadata() const {
        return wb.langItems->dynMetadata();
    }

    const HIRSimplePath& langPointeeSized() const {
        return wb.langItems->pointeeSized();
    }

    const HIRSimplePath& langMetaSized() const {
        return wb.langItems->metaSized();
    }

    const HIRSimplePath& langDestruct() const {
        return wb.langItems->destruct();
    }

    const HIRSimplePath& langFuture() const {
        return wb.langItems->future();
    }

    const HIRSimplePath& langAsyncIterator() const {
        return wb.langItems->asyncIterator();
    }

    const HIRGenericParams* implGenericsPtr() const {
        return implGenerics_;
    }

    const HIRGenericParams* itemGenericsPtr() const {
        return itemGenerics_;
    }

    void forEachTypeEqualityCb(HIRTypeEqualityCallback& cb) {
        for (auto& e : typeEqualities) {
            e.second.ty = cb.visit(e.second.ty);
        }
    }

    template <typename F>
    void forEachTypeEquality(F f) {
        HIRTypeEqualityCb<F> cb(f);
        forEachTypeEqualityCb(cb);
    }

    const WireBoard& board() const {
        return wb;
    }

    const HIRCrate& hirCrate() const {
        return crate;
    }

    const HIRGenericParams* implGenerics_;
    const HIRGenericParams* itemGenerics_;
    HIRGenericParams emptyGenerics_;

    struct CachedEquality {
        const HIRType* ty;
    };

    HIRTypeRefMap<CachedEquality> typeEqualities;

    struct CachedBound {
        const HIRTrait* traitPtr;
        HIRTraitPath::assocListT assoc;
        HIRBoundConstness constness = HIRBoundConstness::Never;
    };

    struct CachedBoundCmp {
        typedef std::pair<const HIRType*, HIRGenericPath> keyT;
        typedef std::pair<const HIRType*, const HIRGenericPath&> refT;
        typedef std::pair<const HIRType*, const HIRSimplePath&> refSpT;

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

    typedef RangeVecMap<std::pair<const HIRType*, HIRGenericPath>, CachedBound, CachedBoundCmp> cachedBoundsT;
    cachedBoundsT traitBounds;

    TraitResolveCommon(const WireBoard& wb);

    bool hasSelf() const {
        return implGenerics_ ? true : false;
    }

    const HIRGenericParams& implGenerics() const;

    const HIRGenericParams& itemGenerics() const;

    const HIRType* getConstParamType(const Span& sp, unsigned binding) const;

    void prepIndexes(const Span& sp);

protected:
    void prepIndexesAddEquality(const Span& sp, const HIRType* longTy, const HIRType* shortTy);
    void prepIndexesAddTraitBound(const Span& sp, const HIRType* type, HIRTraitPath traitPath, bool addParents = true);

    bool iterateBoundsCb(HIRGenericBoundCallback& cb) const;

    template <typename F>
    bool iterateBounds(F f) const {
        HIRGenericBoundCb<F> cb(f);
        return iterateBoundsCb(cb);
    }
};
