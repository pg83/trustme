#pragma once

#include "lang_items.h"
#include "wire_board.h"

#include "hir_hir.h"
#include "hir_path.h"
#include "hir_type_ref.h"
#include "range_vec_map.h"
#include "hir_generic_params.h"

#include <map>
#include <memory>

struct TraitResolveCommon {
    // NOTE: `wb`/`crate` are on their way out as data: a component may expose
    // only methods, and `crate` is just `*wb.crate`. Read them through
    // `board()`/`hirCrate()`; the fields become implementation-private when
    // StaticTraitResolve is flipped to an opaque interface.
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

    // Nullable views of the current generics context (the non-Ptr overloads
    // below assert they are set).
    const HIRGenericParams* implGenericsPtr() const {
        return implGenerics_;
    }

    const HIRGenericParams* itemGenericsPtr() const {
        return itemGenerics_;
    }

    // Visit each cached type-equality target; the cache itself stays internal.
    void forEachTypeEquality(::std::function<void(HIRTypeRef&)> cb) {
        for (auto& e : typeEqualities) {
            cb(e.second.ty);
        }
    }

    const WireBoard& board() const {
        return wb;
    }

    const HIRCrate& hirCrate() const {
        return crate;
    }


    const HIRGenericParams* implGenerics_;
    const HIRGenericParams* itemGenerics_;

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


    TraitResolveCommon(const WireBoard& wb);

    bool hasSelf() const {
        return implGenerics_ ? true : false;
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
