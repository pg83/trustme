#pragma once

#include "hir_hir.h"
#include "hir_path.h"
#include "lang_items.h"
#include "wire_board.h"
#include "hir_type_ref.h"
#include "range_vec_map.h"
#include "hir_generic_params.h"
#include "thin_vector.h"

#include <std/sym/i_map.h>
#include <std/mem/obj_pool.h>

#include <map>
#include <memory>

struct TypingEnvironment;

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
        ASSERT_BUG(Span(), !environment_, stl::StringView("A shared typing environment is not rewritten in place"));
        for (auto& e : localIndex_.typeEqualities) {
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

    HIRCrate& hirCrateMut() const {
        ASSERT_BUG(Span(), wb.crate == &crate, stl::StringView("Mutable HIR access requested for a non-owned crate"));
        return *wb.crate;
    }

    const HIRGenericParams* implGenerics_;
    const HIRGenericParams* itemGenerics_;
    HIRGenericParams emptyGenerics_;

    struct CachedEquality {
        const HIRType* ty;
    };

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

    struct BoundIndex {
        HIRTypeRefMap<CachedEquality> typeEqualities;
        cachedBoundsT traitBounds;
    };

    const BoundIndex& boundIndex() const;

    const cachedBoundsT& traitBounds() const {
        return boundIndex().traitBounds;
    }

    const HIRTypeRefMap<CachedEquality>& typeEqualities() const {
        return boundIndex().typeEqualities;
    }

    const TypingEnvironment* typingEnvironment() const {
        return environment_;
    }

    void buildIndex(const Span& sp, BoundIndex& index) const;

    size_t environmentHash() const;

    bool environmentMatches(const TypingEnvironment& environment) const;

    void cloneEnvironmentInto(TypingEnvironment& environment) const;

    TraitResolveCommon(const WireBoard& wb);

    bool hasSelf() const {
        return implGenerics_ ? true : false;
    }

    const HIRGenericParams& implGenerics() const;

    const HIRGenericParams& itemGenerics() const;

    const HIRType* getConstParamType(const Span& sp, unsigned binding) const;

    void prepIndexes(const Span& sp);

protected:
    BoundIndex localIndex_;
    const TypingEnvironment* environment_ = nullptr;

    void prepIndexesAddEquality(const Span& sp, BoundIndex& index, const HIRType* longTy, const HIRType* shortTy) const;
    void prepIndexesAddTraitBound(const Span& sp, BoundIndex& index, const HIRType* type, HIRTraitPath traitPath, bool addParents = true) const;

    bool iterateBoundsCb(HIRGenericBoundCallback& cb) const;

    template <typename F>
    bool iterateBounds(F f) const {
        HIRGenericBoundCb<F> cb(f);
        return iterateBoundsCb(cb);
    }
};

struct TypingEnvironment {
    size_t hash;
    ThinVector<HIRGenericBound> bounds;
    ThinVector<u8> implSized;
    ThinVector<u8> itemSized;
    ThinVector<const HIRType*> implValueTypes;
    ThinVector<const HIRType*> itemValueTypes;
    TraitResolveCommon::BoundIndex index;
    mutable stl::IntMap<u8> copyAnswers;
    mutable stl::IntMap<u8> dropAnswers;
    mutable stl::IntMap<const HIRType*> normalized;
    TypingEnvironment* next;

    TypingEnvironment(size_t hash, stl::ObjPool* pool, TypingEnvironment* next)
        : hash(hash)
        , copyAnswers(pool)
        , dropAnswers(pool)
        , normalized(pool)
        , next(next)
    {
    }
};

inline const TraitResolveCommon::BoundIndex& TraitResolveCommon::boundIndex() const {
    return environment_ ? environment_->index : localIndex_;
}

struct TypingEnvironmentInterner {
    stl::ObjPool::Ref pool;
    stl::IntMap<TypingEnvironment*> index;
    bool enabled_ = false;

    TypingEnvironmentInterner();

    void enable() {
        enabled_ = true;
    }

    bool enabled() const {
        return enabled_;
    }

    bool current(const TypingEnvironment* environment) const {
        return enabled_ ? environment != nullptr : environment == nullptr;
    }

    const TypingEnvironment* intern(const TraitResolveCommon& resolve, const Span& sp);
};

void TypeckCreateEnvironmentInterner(WireBoard& wb, stl::ObjPool& pool);
